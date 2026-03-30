#ifndef REVCONFIG_CACHE_H
#define REVCONFIG_CACHE_H

#include <stdint.h>

// table=configs
// archive=media
// container=jagfile
// # jagfile = requires 'index' and 'filename'
// index=index.dat
// filename=invback.dat
// # Format = load_sprite_pix8(filelist, "invback.dat", index_file_idx, 0);
// format=pix8
// pix8_atlas_index=0

enum RevConfigFieldKind
{
    RCFIELD_NONE,
    RCFIELD_ITEMTYPE,
    RCFIELD_ITEMNAME,
    RCFIELD_ITEMDONE,
    RCFIELD_CACHE_TABLE,
    RCFIELD_CACHE_ARCHIVE,
    RCFIELD_CACHE_CONTAINER,
    RCFIELD_CACHE_INDEX_FILENAME,
    RCFIELD_CACHE_DATA_FILENAME,
    RCFIELD_CACHE_FORMAT,
    RCFIELD_CACHE_ATLAS_INDEX,
    RCFIELD_CACHE_ATLAS_COUNT,
    RCFIELD_CACHE_TRANSFORM,
    RCFIELD_CACHE_CROP_X,
    RCFIELD_CACHE_CROP_Y,
    RCFIELD_CACHE_CROP_WIDTH,
    RCFIELD_CACHE_CROP_HEIGHT,
    RCFIELD_UICOMPONENT_TYPE,
    RCFIELD_UICOMPONENT_SPRITE,
    RCFIELD_UILAYOUT_COMPONENT,
    RCFIELD_UILAYOUT_X,
    RCFIELD_UILAYOUT_Y,
    RCFIELD_UILAYOUT_WIDTH,
    RCFIELD_UILAYOUT_HEIGHT,
    RCFIELD_UILAYOUT_ANCHOR_X,
    RCFIELD_UILAYOUT_ANCHOR_Y,
    RCFIELD_UILAYOUT_TOP,
    RCFIELD_UILAYOUT_LEFT,
    RCFIELD_UILAYOUT_BOTTOM,
    RCFIELD_UILAYOUT_RIGHT,
    RCFIELD_UILAYOUT_NULL,
};

struct RevConfigField
{
    uint8_t kind;
    char value[64];
};

struct RevConfigBuffer
{
    struct RevConfigField* fields;
    uint32_t field_count;
    uint32_t field_capacity;
};

char const*
revconfig_field_kind_str(enum RevConfigFieldKind kind);

struct RevConfigBuffer*
revconfig_buffer_new(uint32_t hint);

void
revconfig_buffer_free(struct RevConfigBuffer* buffer);

int
revconfig_buffer_push_field(
    struct RevConfigBuffer* buffer,
    enum RevConfigFieldKind kind,
    const char* value);

#endif