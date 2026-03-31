#include "revconfig_load.h"

#include "3rd/ini/ini.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Current [type:name] `type` for keyval dispatch (w/h/anchor vs layout vs component). */
static char s_ini_item_type[64];

static int
push_field(
    struct RevConfigBuffer* revconfig_buffer,
    uint8_t kind,
    const char* value)
{
    return revconfig_buffer_push_field(revconfig_buffer, kind, value);
}

static void
push_element_from_ini_header(
    struct RevConfigBuffer* revconfig_buffer,
    char const* section_header)
{
    char item_type[64] = { 0 };
    char item_name[64] = { 0 };

    const char* space = strchr(section_header, ':');
    if( !space )
        return;

    strncpy(item_type, section_header, space - section_header);
    item_type[space - section_header] = '\0';

    strncpy(item_name, space + 1, sizeof(item_name) - 1);
    item_name[sizeof(item_name) - 1] = '\0';

    printf("Parsed section header: type='%s', name='%s'\n", item_type, item_name);

    strncpy(s_ini_item_type, item_type, sizeof(s_ini_item_type) - 1);
    s_ini_item_type[sizeof(s_ini_item_type) - 1] = '\0';

    push_field(revconfig_buffer, RCFIELD_ITEMTYPE, item_type);
    push_field(revconfig_buffer, RCFIELD_ITEMNAME, item_name);
}

static void
push_field_from_ini_kv(
    struct RevConfigBuffer* vec,
    const char* key,
    const char* value)
{
    uint8_t kind = RCFIELD_NONE;
    if( strcmp(key, "sprite") == 0 )
        kind = RCFIELD_UICOMPONENT_SPRITE;
    else if( strcmp(key, "type") == 0 )
        kind = RCFIELD_UICOMPONENT_TYPE;
    else if( strcmp(key, "c") == 0 )
        kind = RCFIELD_UILAYOUT_COMPONENT;
    else if( strcmp(key, "x") == 0 )
        kind = RCFIELD_UILAYOUT_X;
    else if( strcmp(key, "y") == 0 )
        kind = RCFIELD_UILAYOUT_Y;
    else if( strcmp(key, "w") == 0 )
    {
        if( strcmp(s_ini_item_type, "component") == 0 )
            kind = RCFIELD_UICOMPONENT_WIDTH;
        else if( strcmp(s_ini_item_type, "layout") == 0 )
            kind = RCFIELD_UILAYOUT_WIDTH;
    }
    else if( strcmp(key, "h") == 0 )
    {
        if( strcmp(s_ini_item_type, "component") == 0 )
            kind = RCFIELD_UICOMPONENT_HEIGHT;
        else if( strcmp(s_ini_item_type, "layout") == 0 )
            kind = RCFIELD_UILAYOUT_HEIGHT;
    }
    else if( strcmp(key, "anchor_x") == 0 )
    {
        if( strcmp(s_ini_item_type, "component") == 0 )
            kind = RCFIELD_UICOMPONENT_ANCHOR_X;
        else if( strcmp(s_ini_item_type, "layout") == 0 )
            kind = RCFIELD_UILAYOUT_ANCHOR_X;
    }
    else if( strcmp(key, "anchor_y") == 0 )
    {
        if( strcmp(s_ini_item_type, "component") == 0 )
            kind = RCFIELD_UICOMPONENT_ANCHOR_Y;
        else if( strcmp(s_ini_item_type, "layout") == 0 )
            kind = RCFIELD_UILAYOUT_ANCHOR_Y;
    }
    else if( strcmp(key, "hitbox_x") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_HITBOX_X;
    else if( strcmp(key, "hitbox_y") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_HITBOX_Y;
    else if( strcmp(key, "hitbox_w") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_HITBOX_W;
    else if( strcmp(key, "hitbox_h") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_HITBOX_H;
    else if( strcmp(key, "left") == 0 )
        kind = RCFIELD_UILAYOUT_LEFT;
    else if( strcmp(key, "top") == 0 )
        kind = RCFIELD_UILAYOUT_TOP;
    else if( strcmp(key, "right") == 0 )
        kind = RCFIELD_UILAYOUT_RIGHT;
    else if( strcmp(key, "bottom") == 0 )
        kind = RCFIELD_UILAYOUT_BOTTOM;
    else if( strcmp(key, "table") == 0 )
        kind = RCFIELD_CACHE_TABLE;
    else if( strcmp(key, "archive") == 0 )
        kind = RCFIELD_CACHE_ARCHIVE;
    else if( strcmp(key, "container") == 0 )
        kind = RCFIELD_CACHE_CONTAINER;
    else if( strcmp(key, "index") == 0 )
        kind = RCFIELD_CACHE_INDEX_FILENAME;
    else if( strcmp(key, "filename") == 0 )
        kind = RCFIELD_CACHE_DATA_FILENAME;
    else if( strcmp(key, "format") == 0 )
        kind = RCFIELD_CACHE_FORMAT;
    else if( strcmp(key, "atlas_index") == 0 )
        kind = RCFIELD_CACHE_ATLAS_INDEX;
    else if( strcmp(key, "atlas_count") == 0 )
        kind = RCFIELD_CACHE_ATLAS_COUNT;
    else if( strcmp(key, "crop_x") == 0 )
        kind = RCFIELD_CACHE_CROP_X;
    else if( strcmp(key, "crop_y") == 0 )
        kind = RCFIELD_CACHE_CROP_Y;
    else if( strcmp(key, "crop_width") == 0 )
        kind = RCFIELD_CACHE_CROP_WIDTH;
    else if( strcmp(key, "crop_height") == 0 )
        kind = RCFIELD_CACHE_CROP_HEIGHT;
    else if(
        strcmp(key, "transform1") == 0 || strcmp(key, "transform2") == 0 ||
        strcmp(key, "transform3") == 0 || strcmp(key, "transform4") == 0 )
        kind = RCFIELD_CACHE_TRANSFORM;

    if( kind != RCFIELD_NONE )
        push_field(vec, kind, value);
}

void
revconfig_load_fields_from_ini_bytes(
    const uint8_t* data,
    uint32_t size,
    struct RevConfigBuffer* revconfig_buffer)
{
    if( !data || size == 0 || !revconfig_buffer )
        return;

    s_ini_item_type[0] = '\0';

    struct INIReader reader = { 0 };
    ini_reader_init(&reader);

    struct INIElement element = { 0 };
    while( ini_reader_next(&reader, (uint8_t*)data, size, &element) == 1 )
    {
        switch( element.kind )
        {
        case INI_ELEMENT_SECTION:
            push_element_from_ini_header(revconfig_buffer, element._section.name);
            break;
        case INI_ELEMENT_SECTION_END:
            push_field(revconfig_buffer, RCFIELD_ITEMDONE, "");
            break;
        case INI_ELEMENT_KEYVAL:
            push_field_from_ini_kv(revconfig_buffer, element._keyval.name, element._keyval.value);
            break;
        }
    }

    push_field(revconfig_buffer, RCFIELD_ITEMDONE, "");
    assert(reader.state == INI_READER_STATE_DONE);
}

void
revconfig_load_fields_from_ini(
    const char* filename,
    struct RevConfigBuffer* revconfig_buffer)
{
    FILE* f = fopen(filename, "r");
    if( !f )
        return;

    char* file_data = NULL;
    long file_size = 0;

    if( fseek(f, 0, SEEK_END) != 0 || (file_size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0 )
    {
        fclose(f);
        return;
    }

    file_data = malloc(file_size);
    if( !file_data )
    {
        fclose(f);
        return;
    }

    if( fread(file_data, 1, file_size, f) != (size_t)file_size )
    {
        fclose(f);
        free(file_data);
        return;
    }

    fclose(f);

    revconfig_load_fields_from_ini_bytes(
        (const uint8_t*)file_data, (uint32_t)file_size, revconfig_buffer);
    free(file_data);
}