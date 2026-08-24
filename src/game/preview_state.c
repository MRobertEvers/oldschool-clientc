#include "preview_state.h"

#include "game/rs_player_stats.h"
#include "varc/varc_manager.h"
#include "varp/varp_manager.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char const g_preview_state_magic[8] = {
    'C', '2', 'S', 'T', 'A', 'T', 'E', '1'
};

struct PreviewStateRecord
{
    unsigned kind;
    int id;
    uint32_t payload_size;
    unsigned char const* payload;
};

static void
preview_state_error(
    char* error,
    size_t capacity,
    char const* message)
{
    if( !error || capacity == 0 )
        return;
    snprintf(error, capacity, "%s", message ? message : "invalid preview state");
}

static uint32_t
preview_state_u32(unsigned char const* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int32_t
preview_state_i32(unsigned char const* p)
{
    return (int32_t)preview_state_u32(p);
}

static int
preview_state_decode(
    unsigned char const* bytes,
    size_t size,
    struct VarPManager const* varps,
    struct PreviewStateRecord** records_out,
    uint32_t* count_out,
    char* error,
    size_t error_capacity)
{
    struct PreviewStateRecord* records = NULL;
    uint32_t count;
    size_t offset = 12;
    unsigned previous_kind = 0;
    int previous_id = -1;

    if( !bytes || size < 12 || size > TORIRS_PREVIEW_STATE_MAX_BYTES )
    {
        preview_state_error(error, error_capacity, "preview-state packet size is invalid");
        return 0;
    }
    if( memcmp(bytes, g_preview_state_magic, sizeof(g_preview_state_magic)) != 0 )
    {
        preview_state_error(error, error_capacity, "preview-state packet has the wrong magic");
        return 0;
    }
    count = preview_state_u32(bytes + 8);
    if( count > TORIRS_PREVIEW_STATE_MAX_RECORDS )
    {
        preview_state_error(error, error_capacity, "preview-state record limit exceeded");
        return 0;
    }
    if( count > 0 )
    {
        records = calloc(count, sizeof(*records));
        if( !records )
        {
            preview_state_error(error, error_capacity, "preview-state record allocation failed");
            return 0;
        }
    }

    for( uint32_t i = 0; i < count; i++ )
    {
        struct PreviewStateRecord* record = &records[i];
        uint32_t payload_size;
        int id;
        unsigned kind;

        if( size - offset < 12 )
        {
            preview_state_error(error, error_capacity, "preview-state record header is truncated");
            free(records);
            return 0;
        }
        kind = bytes[offset];
        if( bytes[offset + 1] || bytes[offset + 2] || bytes[offset + 3] )
        {
            preview_state_error(error, error_capacity, "preview-state reserved bytes are non-zero");
            free(records);
            return 0;
        }
        id = preview_state_i32(bytes + offset + 4);
        payload_size = preview_state_u32(bytes + offset + 8);
        offset += 12;

        if( kind < TORIRS_PREVIEW_STATE_VARP || kind > TORIRS_PREVIEW_STATE_STAT )
        {
            preview_state_error(error, error_capacity, "preview-state record kind is invalid");
            free(records);
            return 0;
        }
        if( id < 0 || id > TORIRS_PREVIEW_STATE_MAX_ID )
        {
            preview_state_error(error, error_capacity, "preview-state id is out of range");
            free(records);
            return 0;
        }
        if( kind < previous_kind || (kind == previous_kind && id <= previous_id) )
        {
            preview_state_error(
                error, error_capacity, "preview-state records are duplicated or not canonical");
            free(records);
            return 0;
        }
        if( kind == TORIRS_PREVIEW_STATE_VARBIT &&
            VarPManager_VarbitBaseVar(varps, id) < 0 )
        {
            preview_state_error(error, error_capacity, "preview-state varbit id does not exist");
            free(records);
            return 0;
        }
        if( kind == TORIRS_PREVIEW_STATE_STAT && id >= RS_PLAYER_STATS_SKILL_COUNT )
        {
            preview_state_error(error, error_capacity, "preview-state stat id does not exist");
            free(records);
            return 0;
        }
        if( kind == TORIRS_PREVIEW_STATE_VARC_STRING )
        {
            if( payload_size > TORIRS_PREVIEW_STATE_MAX_STRING )
            {
                preview_state_error(error, error_capacity, "preview-state string is too long");
                free(records);
                return 0;
            }
        }
        else if( payload_size != 4 )
        {
            preview_state_error(error, error_capacity, "preview-state integer payload is not 4 bytes");
            free(records);
            return 0;
        }
        if( payload_size > size - offset )
        {
            preview_state_error(error, error_capacity, "preview-state payload is truncated");
            free(records);
            return 0;
        }
        if( kind == TORIRS_PREVIEW_STATE_VARC_STRING &&
            memchr(bytes + offset, '\0', payload_size) )
        {
            preview_state_error(error, error_capacity, "preview-state string contains NUL");
            free(records);
            return 0;
        }

        record->kind = kind;
        record->id = id;
        record->payload_size = payload_size;
        record->payload = bytes + offset;
        offset += payload_size;
        previous_kind = kind;
        previous_id = id;
    }
    if( offset != size )
    {
        preview_state_error(error, error_capacity, "preview-state packet has trailing bytes");
        free(records);
        return 0;
    }

    *records_out = records;
    *count_out = count;
    return 1;
}

int
ToriRSPreviewState_ApplyBuffer(
    unsigned char const* bytes,
    size_t size,
    struct VarPManager* varps,
    struct VarCManager* varcs,
    struct RS_PlayerStats* stats,
    int* applied_count,
    uint32_t* applied_stat_mask,
    char* error,
    size_t error_capacity)
{
    struct PreviewStateRecord* records = NULL;
    uint32_t count = 0;

    if( applied_count )
        *applied_count = 0;
    if( applied_stat_mask )
        *applied_stat_mask = 0;
    if( error && error_capacity > 0 )
        error[0] = '\0';
    if( !varps || !varcs || !stats )
    {
        preview_state_error(error, error_capacity, "preview-state stores are unavailable");
        return 0;
    }
    if( !preview_state_decode(
            bytes, size, varps, &records, &count, error, error_capacity) )
        return 0;

    for( uint32_t i = 0; i < count; i++ )
    {
        struct PreviewStateRecord const* record = &records[i];
        int value;
        switch( record->kind )
        {
        case TORIRS_PREVIEW_STATE_VARP:
            value = preview_state_i32(record->payload);
            VarPManager_SetVarpOptimistic(varps, record->id, value);
            break;
        case TORIRS_PREVIEW_STATE_VARBIT:
            value = preview_state_i32(record->payload);
            VarPManager_SetVarbitOptimistic(varps, record->id, value);
            break;
        case TORIRS_PREVIEW_STATE_VARC_INT:
            value = preview_state_i32(record->payload);
            VarCManager_SetInt(varcs, record->id, value);
            break;
        case TORIRS_PREVIEW_STATE_VARC_STRING:
        {
            char* value_string = malloc((size_t)record->payload_size + 1);
            if( !value_string )
            {
                /* All structural validation happened before mutation. This is
                 * the only fallible apply operation, so make allocation failure
                 * fatal rather than report a misleading partial success. */
                abort();
            }
            memcpy(value_string, record->payload, record->payload_size);
            value_string[record->payload_size] = '\0';
            VarCManager_SetString(varcs, record->id, value_string);
            free(value_string);
            break;
        }
        case TORIRS_PREVIEW_STATE_STAT:
            value = preview_state_i32(record->payload);
            stats->current_level[record->id] = value;
            stats->base_level[record->id] = value;
            if( applied_stat_mask )
                *applied_stat_mask |= (uint32_t)1u << record->id;
            break;
        default:
            abort();
        }
    }
    free(records);
    RS_PlayerStats_RecomputeCombatLevel(stats);
    if( applied_count )
        *applied_count = (int)count;
    return 1;
}

int
ToriRSPreviewState_ApplyFile(
    char const* path,
    struct VarPManager* varps,
    struct VarCManager* varcs,
    struct RS_PlayerStats* stats,
    int* applied_count,
    uint32_t* applied_stat_mask,
    char* error,
    size_t error_capacity)
{
    unsigned char* bytes = NULL;
    long file_size;
    size_t read_size;
    int result;
    FILE* file;

    if( !path || !path[0] )
    {
        preview_state_error(error, error_capacity, "preview-state path is empty");
        return 0;
    }
    file = fopen(path, "rb");
    if( !file )
    {
        preview_state_error(error, error_capacity, "cannot open preview-state packet");
        return 0;
    }
    if( fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0 ||
        (unsigned long)file_size > TORIRS_PREVIEW_STATE_MAX_BYTES )
    {
        fclose(file);
        preview_state_error(error, error_capacity, "preview-state file size is invalid");
        return 0;
    }
    bytes = malloc(file_size > 0 ? (size_t)file_size : 1);
    if( !bytes )
    {
        fclose(file);
        preview_state_error(error, error_capacity, "preview-state file allocation failed");
        return 0;
    }
    read_size = fread(bytes, 1, (size_t)file_size, file);
    if( fclose(file) != 0 || read_size != (size_t)file_size )
    {
        free(bytes);
        preview_state_error(error, error_capacity, "cannot read preview-state packet");
        return 0;
    }
    result = ToriRSPreviewState_ApplyBuffer(
        bytes,
        read_size,
        varps,
        varcs,
        stats,
        applied_count,
        applied_stat_mask,
        error,
        error_capacity);
    free(bytes);
    return result;
}
