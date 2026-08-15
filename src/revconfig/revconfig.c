#include "revconfig.h"

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

static void
revconfig_strncpy_trimmed(
    char* dest,
    const char* src,
    size_t n)
{
    assert(dest);
    assert(src);
    assert(n > 0);

    while( *src == ' ' || *src == '\t' || *src == '\r' || *src == '\n' )
        src++;

    size_t len = strlen(src);
    while( len > 0 &&
           (src[len - 1] == ' ' || src[len - 1] == '\t' || src[len - 1] == '\r' ||
            src[len - 1] == '\n') )
        len--;

    if( len >= n )
        len = n - 1;
    memcpy(dest, src, len);
    dest[len] = '\0';
}

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
    case RCFIELD_CACHE_ARCHIVE_ID:
        return "RCFIELD_CACHE_ARCHIVE_ID";
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
    case RCFIELD_CACHE_ATLAS_COUNT:
        return "RCFIELD_CACHE_ATLAS_COUNT";
    case RCFIELD_CACHE_TRANSFORM:
        return "RCFIELD_CACHE_TRANSFORM";
    case RCFIELD_CACHE_CROP_X:
        return "RCFIELD_CACHE_CROP_X";
    case RCFIELD_CACHE_CROP_Y:
        return "RCFIELD_CACHE_CROP_Y";
    case RCFIELD_CACHE_CROP_WIDTH:
        return "RCFIELD_CACHE_CROP_WIDTH";
    case RCFIELD_CACHE_CROP_HEIGHT:
        return "RCFIELD_CACHE_CROP_HEIGHT";
    case RCFIELD_CACHE_FONT_NAME:
        return "RCFIELD_CACHE_FONT_NAME";
    case RCFIELD_CACHE_FONT_ID:
        return "RCFIELD_CACHE_FONT_ID";
    case RCFIELD_UICOMPONENT_TYPE:
        return "RCFIELD_UICOMPONENT_TYPE";
    case RCFIELD_UICOMPONENT_SPRITE:
        return "RCFIELD_UICOMPONENT_SPRITE";
    case RCFIELD_UICOMPONENT_WIDTH:
        return "RCFIELD_UICOMPONENT_WIDTH";
    case RCFIELD_UICOMPONENT_HEIGHT:
        return "RCFIELD_UICOMPONENT_HEIGHT";
    case RCFIELD_UICOMPONENT_ANCHOR_X:
        return "RCFIELD_UICOMPONENT_ANCHOR_X";
    case RCFIELD_UICOMPONENT_ANCHOR_Y:
        return "RCFIELD_UICOMPONENT_ANCHOR_Y";
    case RCFIELD_UICOMPONENT_TABNO:
        return "RCFIELD_UICOMPONENT_TABNO";
    case RCFIELD_UICOMPONENT_SPRITE_ACTIVE:
        return "RCFIELD_UICOMPONENT_SPRITE_ACTIVE";
    case RCFIELD_UICOMPONENT_COMPONENTNO:
        return "RCFIELD_UICOMPONENT_COMPONENTNO";
    case RCFIELD_UICOMPONENT_INV:
        return "RCFIELD_UICOMPONENT_INV";
    case RCFIELD_UICOMPONENT_PAINT_LEVELS:
        return "RCFIELD_UICOMPONENT_PAINT_LEVELS";
    case RCFIELD_UICOMPONENT_MMB_ROTATE:
        return "RCFIELD_UICOMPONENT_MMB_ROTATE";
    case RCFIELD_UICOMPONENT_WHEEL_ZOOM:
        return "RCFIELD_UICOMPONENT_WHEEL_ZOOM";
    case RCFIELD_UICOMPONENT_HOTKEY:
        return "RCFIELD_UICOMPONENT_HOTKEY";
    case RCFIELD_HOTKEY_COMPONENT:
        return "RCFIELD_HOTKEY_COMPONENT";
    case RCFIELD_HOTKEY_EFFECT:
        return "RCFIELD_HOTKEY_EFFECT";
    case RCFIELD_UICOMPONENT_COLOR:
        return "RCFIELD_UICOMPONENT_COLOR";
    case RCFIELD_UICOMPONENT_FILLED:
        return "RCFIELD_UICOMPONENT_FILLED";
    case RCFIELD_UICOMPONENT_FONT:
        return "RCFIELD_UICOMPONENT_FONT";
    case RCFIELD_UICOMPONENT_CENTER:
        return "RCFIELD_UICOMPONENT_CENTER";
    case RCFIELD_UICOMPONENT_SHADOWED:
        return "RCFIELD_UICOMPONENT_SHADOWED";
    case RCFIELD_UICOMPONENT_TEXT:
        return "RCFIELD_UICOMPONENT_TEXT";
    case RCFIELD_UICOMPONENT_OPTION:
        return "RCFIELD_UICOMPONENT_OPTION";
    case RCFIELD_UICOMPONENT_OPTION_ACTION:
        return "RCFIELD_UICOMPONENT_OPTION_ACTION";
    case RCFIELD_UICOMPONENT_OP0:
        return "RCFIELD_UICOMPONENT_OP0";
    case RCFIELD_UICOMPONENT_OP1:
        return "RCFIELD_UICOMPONENT_OP1";
    case RCFIELD_UICOMPONENT_OP2:
        return "RCFIELD_UICOMPONENT_OP2";
    case RCFIELD_UICOMPONENT_OP3:
        return "RCFIELD_UICOMPONENT_OP3";
    case RCFIELD_UICOMPONENT_OP4:
        return "RCFIELD_UICOMPONENT_OP4";
    case RCFIELD_UICOMPONENT_OP0_ACTION:
        return "RCFIELD_UICOMPONENT_OP0_ACTION";
    case RCFIELD_UICOMPONENT_OP1_ACTION:
        return "RCFIELD_UICOMPONENT_OP1_ACTION";
    case RCFIELD_UICOMPONENT_OP2_ACTION:
        return "RCFIELD_UICOMPONENT_OP2_ACTION";
    case RCFIELD_UICOMPONENT_OP3_ACTION:
        return "RCFIELD_UICOMPONENT_OP3_ACTION";
    case RCFIELD_UICOMPONENT_OP4_ACTION:
        return "RCFIELD_UICOMPONENT_OP4_ACTION";
    case RCFIELD_UICOMPONENT_BUTTON_TYPE:
        return "RCFIELD_UICOMPONENT_BUTTON_TYPE";
    case RCFIELD_UICOMPONENT_CLIENT_CODE:
        return "RCFIELD_UICOMPONENT_CLIENT_CODE";
    case RCFIELD_UICOMPONENT_CHAT_OP_REPORT_ABUSE:
        return "RCFIELD_UICOMPONENT_CHAT_OP_REPORT_ABUSE";
    case RCFIELD_UICOMPONENT_CHAT_OP_REPORT_ABUSE_ACTION:
        return "RCFIELD_UICOMPONENT_CHAT_OP_REPORT_ABUSE_ACTION";
    case RCFIELD_UICOMPONENT_CHAT_OP_ADD_IGNORE:
        return "RCFIELD_UICOMPONENT_CHAT_OP_ADD_IGNORE";
    case RCFIELD_UICOMPONENT_CHAT_OP_ADD_IGNORE_ACTION:
        return "RCFIELD_UICOMPONENT_CHAT_OP_ADD_IGNORE_ACTION";
    case RCFIELD_UICOMPONENT_CHAT_OP_ADD_FRIEND:
        return "RCFIELD_UICOMPONENT_CHAT_OP_ADD_FRIEND";
    case RCFIELD_UICOMPONENT_CHAT_OP_ADD_FRIEND_ACTION:
        return "RCFIELD_UICOMPONENT_CHAT_OP_ADD_FRIEND_ACTION";
    case RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_TRADE:
        return "RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_TRADE";
    case RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_TRADE_ACTION:
        return "RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_TRADE_ACTION";
    case RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_DUEL:
        return "RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_DUEL";
    case RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_DUEL_ACTION:
        return "RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_DUEL_ACTION";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_FILTER:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_FILTER";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_LABEL:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_LABEL";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_LABEL_Y:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_LABEL_Y";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE_Y:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE_Y";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE0:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE0";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE1:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE1";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE2:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE2";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE3:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE3";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE0_COLOR:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE0_COLOR";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE1_COLOR:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE1_COLOR";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE2_COLOR:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE2_COLOR";
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE3_COLOR:
        return "RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE3_COLOR";
    case RCFIELD_INV_ITEM:
        return "RCFIELD_INV_ITEM";
    case RCFIELD_UILAYOUT_COMPONENT:
        return "RCFIELD_UILAYOUT_COMPONENT";
    case RCFIELD_UILAYOUT_X:
        return "RCFIELD_UILAYOUT_X";
    case RCFIELD_UILAYOUT_Y:
        return "RCFIELD_UILAYOUT_Y";
    case RCFIELD_UILAYOUT_WIDTH:
        return "RCFIELD_UILAYOUT_WIDTH";
    case RCFIELD_UILAYOUT_HEIGHT:
        return "RCFIELD_UILAYOUT_HEIGHT";
    case RCFIELD_UILAYOUT_ANCHOR_X:
        return "RCFIELD_UILAYOUT_ANCHOR_X";
    case RCFIELD_UILAYOUT_ANCHOR_Y:
        return "RCFIELD_UILAYOUT_ANCHOR_Y";
    case RCFIELD_UILAYOUT_TOP:
        return "RCFIELD_UILAYOUT_TOP";
    case RCFIELD_UILAYOUT_LEFT:
        return "RCFIELD_UILAYOUT_LEFT";
    case RCFIELD_UILAYOUT_BOTTOM:
        return "RCFIELD_UILAYOUT_BOTTOM";
    case RCFIELD_UILAYOUT_RIGHT:
        return "RCFIELD_UILAYOUT_RIGHT";
    case RCFIELD_UILAYOUT_DIRTY:
        return "RCFIELD_UILAYOUT_DIRTY";
    case RCFIELD_UILAYOUT_PARENT:
        return "RCFIELD_UILAYOUT_PARENT";
    case RCFIELD_UILAYOUT_NAME:
        return "RCFIELD_UILAYOUT_NAME";
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
    assert(buffer);

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
    assert(value);
    revconfig_strncpy_trimmed(field->value, value, sizeof(field->value));
    return 0;
}

struct RevConfigItemBuffer*
revconfig_item_buffer_new(uint32_t hint)
{
    struct RevConfigItemBuffer* buffer = malloc(sizeof(struct RevConfigItemBuffer));
    if( !buffer )
        return NULL;
    memset(buffer, 0, sizeof(struct RevConfigItemBuffer));

    if( hint > 0 )
    {
        buffer->items = malloc(sizeof(struct RevConfigItem) * hint);
        if( !buffer->items )
        {
            free(buffer);
            return NULL;
        }
        buffer->item_capacity = hint;
    }

    return buffer;
}

void
revconfig_item_buffer_free(struct RevConfigItemBuffer* buffer)
{
    if( !buffer )
        return;
    free(buffer->items);
    free(buffer);
}

struct RevConfigItem*
revconfig_item_buffer_push(struct RevConfigItemBuffer* buffer)
{
    assert(buffer);

    if( buffer->item_count >= buffer->item_capacity )
    {
        uint32_t new_capacity = buffer->item_capacity == 0 ? 16 : buffer->item_capacity * 2;
        struct RevConfigItem* new_items =
            realloc(buffer->items, sizeof(struct RevConfigItem) * new_capacity);
        if( !new_items )
            return NULL;
        buffer->items = new_items;
        buffer->item_capacity = new_capacity;
    }

    struct RevConfigItem* item = &buffer->items[buffer->item_count++];
    memset(item, 0, sizeof(*item));
    return item;
}

static void
revconfig_item_set_name(
    struct RevConfigItem* item,
    const char* value)
{
    assert(item);
    assert(value);

    switch( item->kind )
    {
    case RCITEM_CACHE_SPRITE:
        strncpy(item->u.cache.name, value, sizeof(item->u.cache.name) - 1);
        break;
    case RCITEM_CACHE_FONT:
        strncpy(item->u.font.name, value, sizeof(item->u.font.name) - 1);
        break;
    case RCITEM_UICOMPONENT:
        strncpy(item->u.uicomponent.name, value, sizeof(item->u.uicomponent.name) - 1);
        break;
    case RCITEM_UILAYOUT:
        strncpy(item->u.uilayout.name, value, sizeof(item->u.uilayout.name) - 1);
        break;
    case RCITEM_INV:
        strncpy(item->u.inv.name, value, sizeof(item->u.inv.name) - 1);
        break;
    case RCITEM_HOTKEY:
        strncpy(item->u.hotkey.name, value, sizeof(item->u.hotkey.name) - 1);
        break;
    default:
        break;
    }
}

static void
revconfig_item_begin(
    struct RevConfigItem* item,
    const char* type_value)
{
    memset(item, 0, sizeof(*item));

    if( strcmp(type_value, "sprite") == 0 )
    {
        item->kind = RCITEM_CACHE_SPRITE;
        item->u.cache.archive_id = -1;
    }
    else if( strcmp(type_value, "font") == 0 )
    {
        item->kind = RCITEM_CACHE_FONT;
        item->u.font.archive_id = -1;
        item->u.font.cache_font_id = -1;
    }
    else if( strcmp(type_value, "component") == 0 )
    {
        item->kind = RCITEM_UICOMPONENT;
        item->u.uicomponent.componentno = -1;
        /* type=world camera gestures: on unless a revision opts out, so packs
         * that predate the keys (and the cache-interface build path, which has
         * no revconfig section at all) behave the same way. */
        item->u.uicomponent.mmb_rotate = 1;
        item->u.uicomponent.wheel_zoom = 1;
    }
    else if( strcmp(type_value, "layout") == 0 )
        item->kind = RCITEM_UILAYOUT;
    else if( strcmp(type_value, "inv") == 0 )
        item->kind = RCITEM_INV;
    else if( strcmp(type_value, "hotkey") == 0 )
        item->kind = RCITEM_HOTKEY;
    else
        item->kind = RCITEM_NONE;
}

static void
revconfig_item_apply_cache_field(
    struct RevConfigCacheItem* cache,
    enum RevConfigFieldKind kind,
    const char* value)
{
    switch( kind )
    {
    case RCFIELD_CACHE_TABLE:
        strncpy(cache->table, value, sizeof(cache->table) - 1);
        break;
    case RCFIELD_CACHE_ARCHIVE:
        strncpy(cache->archive, value, sizeof(cache->archive) - 1);
        break;
    case RCFIELD_CACHE_ARCHIVE_ID:
        cache->archive_id = atoi(value);
        break;
    case RCFIELD_CACHE_CONTAINER:
        strncpy(cache->container, value, sizeof(cache->container) - 1);
        break;
    case RCFIELD_CACHE_INDEX_FILENAME:
        strncpy(cache->index_filename, value, sizeof(cache->index_filename) - 1);
        break;
    case RCFIELD_CACHE_DATA_FILENAME:
        strncpy(cache->data_filename, value, sizeof(cache->data_filename) - 1);
        break;
    case RCFIELD_CACHE_FORMAT:
        strncpy(cache->format, value, sizeof(cache->format) - 1);
        break;
    case RCFIELD_CACHE_ATLAS_INDEX:
        cache->atlas_index = atoi(value);
        break;
    case RCFIELD_CACHE_ATLAS_COUNT:
        cache->atlas_count = atoi(value);
        break;
    case RCFIELD_CACHE_TRANSFORM:
        if( cache->transform_count < 4 )
        {
            strncpy(
                cache->transform[cache->transform_count],
                value,
                sizeof(cache->transform[cache->transform_count]) - 1);
            cache->transform_count++;
        }
        break;
    case RCFIELD_CACHE_CROP_X:
        cache->crop_x = atoi(value);
        break;
    case RCFIELD_CACHE_CROP_Y:
        cache->crop_y = atoi(value);
        break;
    case RCFIELD_CACHE_CROP_WIDTH:
        cache->crop_width = atoi(value);
        break;
    case RCFIELD_CACHE_CROP_HEIGHT:
        cache->crop_height = atoi(value);
        break;
    default:
        break;
    }
}

static void
revconfig_item_apply_font_field(
    struct RevConfigFontItem* font,
    enum RevConfigFieldKind kind,
    const char* value)
{
    switch( kind )
    {
    case RCFIELD_CACHE_TABLE:
        strncpy(font->table, value, sizeof(font->table) - 1);
        break;
    case RCFIELD_CACHE_ARCHIVE:
        strncpy(font->archive, value, sizeof(font->archive) - 1);
        break;
    case RCFIELD_CACHE_ARCHIVE_ID:
        font->archive_id = atoi(value);
        break;
    case RCFIELD_CACHE_FONT_NAME:
        strncpy(font->font_name, value, sizeof(font->font_name) - 1);
        break;
    case RCFIELD_CACHE_FONT_ID:
        font->cache_font_id = atoi(value);
        break;
    default:
        break;
    }
}

static int
revconfig_font_field_is_numeric(const char* value)
{
    assert(value);
    if( value[0] == '\0' )
        return 0;
    for( const char* p = value; *p; p++ )
    {
        if( *p < '0' || *p > '9' )
            return 0;
    }
    return 1;
}

static int
revconfig_minimenu_action_from_symbol(char const* sym)
{
    assert(sym);
    if( sym[0] == '\0' )
        return 0;

#define MAP_ACTION(name)                                                                           \
    if( strcasecmp(sym, #name) == 0 )                                                              \
        return REVCONFIG_MINIMENU_##name;

    MAP_ACTION(CANCEL)
    MAP_ACTION(WALK)
    MAP_ACTION(IF_BUTTON)
    MAP_ACTION(IF_BUTTON_TOGGLE)
    MAP_ACTION(IF_BUTTON_SELECT)
    MAP_ACTION(RESUME_PAUSEBUTTON)
    MAP_ACTION(CLOSE_MODAL)
    MAP_ACTION(INV_BUTTON1)
    MAP_ACTION(INV_BUTTON2)
    MAP_ACTION(INV_BUTTON3)
    MAP_ACTION(INV_BUTTON4)
    MAP_ACTION(INV_BUTTON5)
    MAP_ACTION(FRIENDLIST_ADD)
    MAP_ACTION(IGNORELIST_ADD)
    MAP_ACTION(FRIENDLIST_DEL)
    MAP_ACTION(IGNORELIST_DEL)
    MAP_ACTION(MESSAGE_PRIVATE)
    MAP_ACTION(REPORT_ABUSE)
    MAP_ACTION(OPHELD1)
    MAP_ACTION(OPHELD2)
    MAP_ACTION(OPHELD3)
    MAP_ACTION(OPHELD4)
    MAP_ACTION(OPHELD5)
    MAP_ACTION(OPHELD6)
    /* Client.ts aliases */
    if( strcasecmp(sym, "CLOSE_BUTTON") == 0 )
        return REVCONFIG_MINIMENU_CLOSE_MODAL;
    if( strcasecmp(sym, "TOGGLE_BUTTON") == 0 )
        return REVCONFIG_MINIMENU_IF_BUTTON_TOGGLE;
    if( strcasecmp(sym, "SELECT_BUTTON") == 0 )
        return REVCONFIG_MINIMENU_IF_BUTTON_SELECT;
    if( strcasecmp(sym, "PAUSE_BUTTON") == 0 )
        return REVCONFIG_MINIMENU_RESUME_PAUSEBUTTON;
    if( strcasecmp(sym, "ABUSE_REPORT") == 0 )
        return REVCONFIG_MINIMENU_REPORT_ABUSE;
    if( strcasecmp(sym, "ACCEPT_TRADEREQ") == 0 )
        return REVCONFIG_MINIMENU_OPPLAYER_TRADEREQ;
    if( strcasecmp(sym, "ACCEPT_DUELREQ") == 0 )
        return REVCONFIG_MINIMENU_OPPLAYER_DUELREQ;

#undef MAP_ACTION
    return 0;
}

int
revconfig_parse_minimenu_action(char const* str)
{
    assert(str);
    if( str[0] == '\0' )
        return 0;

    int sym = revconfig_minimenu_action_from_symbol(str);
    if( sym != 0 )
        return sym;

    char* end = NULL;
    long v = strtol(str, &end, 10);
    if( end != str && *end == '\0' && v > 0 )
        return (int)v;

    fprintf(stderr, "revconfig_parse_minimenu_action: unknown action '%s'\n", str);
    assert(false && "unknown minimenu action in revconfig");
    return 0;
}

static int
revconfig_parse_chat_button_filter(char const* value)
{
    assert(value);
    if( value[0] == '\0' )
        return -1;
    if( strcasecmp(value, "public") == 0 )
        return 0;
    if( strcasecmp(value, "private") == 0 )
        return 1;
    if( strcasecmp(value, "trade") == 0 )
        return 2;
    if( strcasecmp(value, "report") == 0 )
        return 3;
    return atoi(value);
}

int
revconfig_parse_button_type(char const* str)
{
    assert(str);
    if( str[0] == '\0' )
        return 0;

    if( strcasecmp(str, "ok") == 0 )
        return REVCONFIG_BUTTON_TYPE_OK;
    if( strcasecmp(str, "target") == 0 )
        return REVCONFIG_BUTTON_TYPE_TARGET;
    if( strcasecmp(str, "close") == 0 )
        return REVCONFIG_BUTTON_TYPE_CLOSE;
    if( strcasecmp(str, "toggle") == 0 )
        return REVCONFIG_BUTTON_TYPE_TOGGLE;
    if( strcasecmp(str, "select") == 0 )
        return REVCONFIG_BUTTON_TYPE_SELECT;
    if( strcasecmp(str, "continue") == 0 )
        return REVCONFIG_BUTTON_TYPE_CONTINUE;

    return atoi(str);
}

static void
revconfig_item_apply_uicomponent_field(
    struct RevConfigUIComponentItem* comp,
    enum RevConfigFieldKind kind,
    const char* value)
{
    switch( kind )
    {
    case RCFIELD_UICOMPONENT_TYPE:
        strncpy(comp->type, value, sizeof(comp->type) - 1);
        break;
    case RCFIELD_UICOMPONENT_SPRITE:
        strncpy(comp->sprite, value, sizeof(comp->sprite) - 1);
        break;
    case RCFIELD_UICOMPONENT_WIDTH:
        comp->width = atoi(value);
        break;
    case RCFIELD_UICOMPONENT_HEIGHT:
        comp->height = atoi(value);
        break;
    case RCFIELD_UICOMPONENT_ANCHOR_X:
        comp->anchor_x = atoi(value);
        break;
    case RCFIELD_UICOMPONENT_ANCHOR_Y:
        comp->anchor_y = atoi(value);
        break;
    case RCFIELD_UICOMPONENT_TABNO:
        comp->tabno = atoi(value);
        break;
    case RCFIELD_UICOMPONENT_SELECTED:
        comp->selected = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) ? 1 : 0;
        break;
    case RCFIELD_UICOMPONENT_SLOT:
        strncpy(comp->slot, value, sizeof(comp->slot) - 1);
        comp->slot[sizeof(comp->slot) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_SPRITE_ACTIVE:
        strncpy(comp->sprite_active, value, sizeof(comp->sprite_active) - 1);
        break;
    case RCFIELD_UICOMPONENT_COMPONENTNO:
        comp->componentno = atoi(value);
        break;
    case RCFIELD_UICOMPONENT_INV:
        strncpy(comp->inv, value, sizeof(comp->inv) - 1);
        break;
    case RCFIELD_UICOMPONENT_PAINT_LEVELS:
        strncpy(comp->paint_levels, value, sizeof(comp->paint_levels) - 1);
        comp->paint_levels[sizeof(comp->paint_levels) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_MMB_ROTATE:
        comp->mmb_rotate = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) ? 1 : 0;
        break;
    case RCFIELD_UICOMPONENT_WHEEL_ZOOM:
        comp->wheel_zoom = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) ? 1 : 0;
        break;
    case RCFIELD_UICOMPONENT_HOTKEY:
        /* Repeatable, like transform= and inv item=: each line appends. */
        if( comp->hotkey_count < REVCONFIG_COMPONENT_HOTKEY_MAX )
        {
            strncpy(
                comp->hotkeys[comp->hotkey_count],
                value,
                sizeof(comp->hotkeys[comp->hotkey_count]) - 1);
            comp->hotkeys[comp->hotkey_count][sizeof(comp->hotkeys[0]) - 1] = '\0';
            comp->hotkey_count++;
        }
        break;
    case RCFIELD_UICOMPONENT_COLOR:
        comp->color = atoi(value);
        break;
    case RCFIELD_UICOMPONENT_FILLED:
        comp->filled = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) ? 1 : 0;
        break;
    case RCFIELD_UICOMPONENT_FONT:
        if( revconfig_font_field_is_numeric(value) )
        {
            comp->font = atoi(value);
            comp->has_font_ref = 0;
            comp->font_ref[0] = '\0';
        }
        else
        {
            strncpy(comp->font_ref, value, sizeof(comp->font_ref) - 1);
            comp->font_ref[sizeof(comp->font_ref) - 1] = '\0';
            comp->has_font_ref = 1;
        }
        break;
    case RCFIELD_UICOMPONENT_CENTER:
        comp->center = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) ? 1 : 0;
        break;
    case RCFIELD_UICOMPONENT_SHADOWED:
        comp->shadowed = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) ? 1 : 0;
        break;
    case RCFIELD_UICOMPONENT_TEXT:
        strncpy(comp->text, value, sizeof(comp->text) - 1);
        comp->text[sizeof(comp->text) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_OPTION:
        strncpy(comp->option, value, sizeof(comp->option) - 1);
        comp->option[sizeof(comp->option) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_OPTION_ACTION:
        comp->option_action = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_OP0:
        strncpy(comp->ops[0], value, sizeof(comp->ops[0]) - 1);
        comp->ops[0][sizeof(comp->ops[0]) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_OP1:
        strncpy(comp->ops[1], value, sizeof(comp->ops[1]) - 1);
        comp->ops[1][sizeof(comp->ops[1]) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_OP2:
        strncpy(comp->ops[2], value, sizeof(comp->ops[2]) - 1);
        comp->ops[2][sizeof(comp->ops[2]) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_OP3:
        strncpy(comp->ops[3], value, sizeof(comp->ops[3]) - 1);
        comp->ops[3][sizeof(comp->ops[3]) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_OP4:
        strncpy(comp->ops[4], value, sizeof(comp->ops[4]) - 1);
        comp->ops[4][sizeof(comp->ops[4]) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_OP0_ACTION:
        comp->op_actions[0] = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_OP1_ACTION:
        comp->op_actions[1] = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_OP2_ACTION:
        comp->op_actions[2] = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_OP3_ACTION:
        comp->op_actions[3] = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_OP4_ACTION:
        comp->op_actions[4] = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_BUTTON_TYPE:
        comp->button_type = revconfig_parse_button_type(value);
        break;
    case RCFIELD_UICOMPONENT_CLIENT_CODE:
        comp->client_code = atoi(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_OP_REPORT_ABUSE:
        strncpy(comp->chat_op_report_abuse, value, sizeof(comp->chat_op_report_abuse) - 1);
        comp->chat_op_report_abuse[sizeof(comp->chat_op_report_abuse) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_CHAT_OP_REPORT_ABUSE_ACTION:
        comp->chat_op_report_abuse_action = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_OP_ADD_IGNORE:
        strncpy(comp->chat_op_add_ignore, value, sizeof(comp->chat_op_add_ignore) - 1);
        comp->chat_op_add_ignore[sizeof(comp->chat_op_add_ignore) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_CHAT_OP_ADD_IGNORE_ACTION:
        comp->chat_op_add_ignore_action = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_OP_ADD_FRIEND:
        strncpy(comp->chat_op_add_friend, value, sizeof(comp->chat_op_add_friend) - 1);
        comp->chat_op_add_friend[sizeof(comp->chat_op_add_friend) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_CHAT_OP_ADD_FRIEND_ACTION:
        comp->chat_op_add_friend_action = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_TRADE:
        strncpy(comp->chat_op_accept_trade, value, sizeof(comp->chat_op_accept_trade) - 1);
        comp->chat_op_accept_trade[sizeof(comp->chat_op_accept_trade) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_TRADE_ACTION:
        comp->chat_op_accept_trade_action = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_DUEL:
        strncpy(comp->chat_op_accept_duel, value, sizeof(comp->chat_op_accept_duel) - 1);
        comp->chat_op_accept_duel[sizeof(comp->chat_op_accept_duel) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_DUEL_ACTION:
        comp->chat_op_accept_duel_action = revconfig_parse_minimenu_action(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_FILTER:
        comp->chat_button_filter = revconfig_parse_chat_button_filter(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_LABEL:
        strncpy(comp->chat_button_label, value, sizeof(comp->chat_button_label) - 1);
        comp->chat_button_label[sizeof(comp->chat_button_label) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_LABEL_Y:
        comp->chat_button_label_y = atoi(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE_Y:
        comp->chat_button_mode_y = atoi(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE0:
        strncpy(comp->chat_button_mode_label[0], value, sizeof(comp->chat_button_mode_label[0]) - 1);
        comp->chat_button_mode_label[0][sizeof(comp->chat_button_mode_label[0]) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE1:
        strncpy(comp->chat_button_mode_label[1], value, sizeof(comp->chat_button_mode_label[1]) - 1);
        comp->chat_button_mode_label[1][sizeof(comp->chat_button_mode_label[1]) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE2:
        strncpy(comp->chat_button_mode_label[2], value, sizeof(comp->chat_button_mode_label[2]) - 1);
        comp->chat_button_mode_label[2][sizeof(comp->chat_button_mode_label[2]) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE3:
        strncpy(comp->chat_button_mode_label[3], value, sizeof(comp->chat_button_mode_label[3]) - 1);
        comp->chat_button_mode_label[3][sizeof(comp->chat_button_mode_label[3]) - 1] = '\0';
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE0_COLOR:
        comp->chat_button_mode_color[0] = atoi(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE1_COLOR:
        comp->chat_button_mode_color[1] = atoi(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE2_COLOR:
        comp->chat_button_mode_color[2] = atoi(value);
        break;
    case RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE3_COLOR:
        comp->chat_button_mode_color[3] = atoi(value);
        break;
    default:
        break;
    }
}

static void
revconfig_item_apply_uilayout_field(
    struct RevConfigUILayoutItem* layout,
    enum RevConfigFieldKind kind,
    const char* value)
{
    switch( kind )
    {
    case RCFIELD_UILAYOUT_COMPONENT:
        strncpy(layout->component, value, sizeof(layout->component) - 1);
        break;
    case RCFIELD_UILAYOUT_X:
        layout->x = atoi(value);
        break;
    case RCFIELD_UILAYOUT_Y:
        layout->y = atoi(value);
        break;
    case RCFIELD_UILAYOUT_WIDTH:
        layout->width = atoi(value);
        break;
    case RCFIELD_UILAYOUT_HEIGHT:
        layout->height = atoi(value);
        break;
    case RCFIELD_UILAYOUT_ANCHOR_X:
        layout->anchor_x = atoi(value);
        layout->has_anchor = 1;
        break;
    case RCFIELD_UILAYOUT_ANCHOR_Y:
        layout->anchor_y = atoi(value);
        layout->has_anchor = 1;
        break;
    case RCFIELD_UILAYOUT_TOP:
        layout->top = atoi(value);
        break;
    case RCFIELD_UILAYOUT_LEFT:
        layout->left = atoi(value);
        break;
    case RCFIELD_UILAYOUT_BOTTOM:
        layout->bottom = atoi(value);
        break;
    case RCFIELD_UILAYOUT_RIGHT:
        layout->right = atoi(value);
        break;
    case RCFIELD_UILAYOUT_DIRTY:
        layout->dirty = 1;
        break;
    case RCFIELD_UILAYOUT_PARENT:
        strncpy(layout->parent, value, sizeof(layout->parent) - 1);
        break;
    case RCFIELD_UILAYOUT_NAME:
        strncpy(layout->name, value, sizeof(layout->name) - 1);
        break;
    case RCFIELD_UILAYOUT_GROUP:
        strncpy(layout->layout_group, value, sizeof(layout->layout_group) - 1);
        break;
    default:
        break;
    }
}

static void
revconfig_item_apply_field(
    struct RevConfigItem* item,
    enum RevConfigFieldKind kind,
    const char* value)
{
    assert(item);
    if( item->kind == RCITEM_NONE )
        return;

    switch( item->kind )
    {
    case RCITEM_CACHE_SPRITE:
        revconfig_item_apply_cache_field(&item->u.cache, kind, value);
        break;
    case RCITEM_CACHE_FONT:
        revconfig_item_apply_font_field(&item->u.font, kind, value);
        break;
    case RCITEM_UICOMPONENT:
        revconfig_item_apply_uicomponent_field(&item->u.uicomponent, kind, value);
        break;
    case RCITEM_UILAYOUT:
        revconfig_item_apply_uilayout_field(&item->u.uilayout, kind, value);
        break;
    case RCITEM_INV:
        if( kind == RCFIELD_INV_ITEM && item->u.inv.item_count < REVCONFIG_INV_MAX_ITEMS )
        {
            strncpy(
                item->u.inv.items[item->u.inv.item_count],
                value,
                sizeof(item->u.inv.items[item->u.inv.item_count]) - 1);
            item->u.inv.item_count++;
        }
        break;
    case RCITEM_HOTKEY:
        if( kind == RCFIELD_HOTKEY_COMPONENT )
        {
            strncpy(item->u.hotkey.component, value, sizeof(item->u.hotkey.component) - 1);
            item->u.hotkey.component[sizeof(item->u.hotkey.component) - 1] = '\0';
        }
        else if( kind == RCFIELD_HOTKEY_EFFECT )
        {
            strncpy(item->u.hotkey.effect, value, sizeof(item->u.hotkey.effect) - 1);
            item->u.hotkey.effect[sizeof(item->u.hotkey.effect) - 1] = '\0';
        }
        break;
    default:
        break;
    }
}

static void
revconfig_item_finish(
    struct RevConfigItem* pending,
    struct RevConfigItemBuffer* out)
{
    assert(pending);
    assert(out);
    if( pending->kind == RCITEM_NONE )
        return;

    struct RevConfigItem* item = revconfig_item_buffer_push(out);
    if( !item )
        return;

    *item = *pending;
    pending->kind = RCITEM_NONE;
}

void
revconfig_items_build(
    const struct RevConfigBuffer* fields,
    struct RevConfigItemBuffer* out)
{
    assert(fields);
    assert(out);

    struct RevConfigItem pending = { 0 };

    for( uint32_t i = 0; i < fields->field_count; i++ )
    {
        const struct RevConfigField* field = &fields->fields[i];

        switch( field->kind )
        {
        case RCFIELD_ITEMTYPE:
            revconfig_item_begin(&pending, field->value);
            break;
        case RCFIELD_ITEMNAME:
            revconfig_item_set_name(&pending, field->value);
            break;
        case RCFIELD_ITEMDONE:
            revconfig_item_finish(&pending, out);
            break;
        default:
            revconfig_item_apply_field(&pending, field->kind, field->value);
            break;
        }
    }
}