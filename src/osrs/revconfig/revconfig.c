#include "revconfig.h"

#include "osrs/rscache/tables/string_utils.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

char const*
revconfig_field_kind_str(enum RevConfigFieldKind kind)
{
    switch( kind )
    {
    case RCFIELD_NONE:
        return "RCFIELD_NONE";
    case RCFIELD_ITEMTYPE:
        return "RCFIELD_ITEMTYPE";
    case RCFIELD_ITEMNAME:
        return "RCFIELD_ITEMNAME";
    case RCFIELD_ITEMDONE:
        return "RCFIELD_ITEMDONE";
    case RCFIELD_CACHE_TABLE:
        return "RCFIELD_CACHE_TABLE";
    case RCFIELD_CACHE_ARCHIVE:
        return "RCFIELD_CACHE_ARCHIVE";
    case RCFIELD_CACHE_CONTAINER:
        return "RCFIELD_CACHE_CONTAINER";
    case RCFIELD_CACHE_INDEX_FILENAME:
        return "RCFIELD_CACHE_INDEX_FILENAME";
    case RCFIELD_CACHE_DATA_FILENAME:
        return "RCFIELD_CACHE_DATA_FILENAME";
    case RCFIELD_CACHE_FORMAT:
        return "RCFIELD_CACHE_FORMAT";
    case RCFIELD_CACHE_ATLAS_INDEX:
        return "RCFIELD_CACHE_ATLAS_INDEX";
    case RCFIELD_UICOMPONENT_SPRITE:
        return "RCFIELD_UICOMPONENT_SPRITE";
    case RCFIELD_UILAYOUT_X:
        return "RCFIELD_UILAYOUT_X";
    case RCFIELD_UILAYOUT_Y:
        return "RCFIELD_UILAYOUT_Y";
    case RCFIELD_UILAYOUT_TOP:
        return "RCFIELD_UILAYOUT_TOP";
    case RCFIELD_UILAYOUT_LEFT:
        return "RCFIELD_UILAYOUT_LEFT";
    case RCFIELD_UILAYOUT_BOTTOM:
        return "RCFIELD_UILAYOUT_BOTTOM";
    case RCFIELD_UILAYOUT_RIGHT:
        return "RCFIELD_UILAYOUT_RIGHT";
    default:
        return "UNKNOWN";
    }
}

struct RevConfigBuffer*
revconfig_buffer_new(uint32_t hint)
{
    struct RevConfigBuffer* buffer = malloc(sizeof(struct RevConfigBuffer));
    if( !buffer )
        return NULL;
    memset(buffer, 0, sizeof(struct RevConfigBuffer));

    if( hint > 0 )
    {
        buffer->fields = malloc(sizeof(struct RevConfigField) * hint);
        if( !buffer->fields )
        {
            free(buffer);
            return NULL;
        }
        buffer->field_capacity = hint;
    }

    return buffer;
}

void
revconfig_buffer_free(struct RevConfigBuffer* buffer)
{
    if( !buffer )
        return;
    free(buffer->fields);
    free(buffer);
}

int
revconfig_buffer_push_field(
    struct RevConfigBuffer* buffer,
    enum RevConfigFieldKind kind,
    const char* value)
{
    if( buffer->field_count >= buffer->field_capacity )
    {
        uint32_t new_capacity = buffer->field_capacity == 0 ? 16 : buffer->field_capacity * 2;
        struct RevConfigField* new_fields =
            realloc(buffer->fields, sizeof(struct RevConfigField) * new_capacity);
        if( !new_fields )
            return -1;
        buffer->fields = new_fields;
        buffer->field_capacity = new_capacity;
    }

    struct RevConfigField* field = &buffer->fields[buffer->field_count++];
    field->kind = kind;
    strncpy_trimmed(field->value, value, sizeof(field->value), TRIM_CHARS_WHITESPACE);
    return 0;
}