/*
 * Static UI buffer: load elements from INI + order file, execute draw commands each frame.
 * INI format: [element_id], keys: sprite, x, y, state_handler (optional), draw_type (optional).
 * Position x/y can be integer or expression: chat_y, bind_x, bind_y, bind_bottom_x, bind_bottom_y.
 * Sprite: game struct path e.g. sprite_invback or sprite_sideicons[3].
 */

#include "static_ui_buffer.h"

#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DESCRIPTORS 128
#define MAX_LINE 256
#define INI_MAX_KEY 64
#define INI_MAX_VAL 128

/* ---- Sprite lookup: key -> (offset in GGame, array size). array_size 0 = single sprite. ---- */
struct SpriteTableEntry
{
    const char* key;
    size_t offset;
    int array_size;
};

static const struct SpriteTableEntry sprite_table[] = {
    { "sprite_invback",     offsetof(struct GGame, sprite_invback),     0  },
    { "sprite_chatback",    offsetof(struct GGame, sprite_chatback),    0  },
    { "sprite_mapback",     offsetof(struct GGame, sprite_mapback),     0  },
    { "sprite_backbase1",   offsetof(struct GGame, sprite_backbase1),   0  },
    { "sprite_backbase2",   offsetof(struct GGame, sprite_backbase2),   0  },
    { "sprite_backhmid1",   offsetof(struct GGame, sprite_backhmid1),   0  },
    { "sprite_backhmid2",   offsetof(struct GGame, sprite_backhmid2),   0  },
    { "sprite_sideicons",   offsetof(struct GGame, sprite_sideicons),   13 },
    { "sprite_compass",     offsetof(struct GGame, sprite_compass),     0  },
    { "sprite_redstone1",   offsetof(struct GGame, sprite_redstone1),   0  },
    { "sprite_redstone2",   offsetof(struct GGame, sprite_redstone2),   0  },
    { "sprite_redstone3",   offsetof(struct GGame, sprite_redstone3),   0  },
    { "sprite_redstone1h",  offsetof(struct GGame, sprite_redstone1h),  0  },
    { "sprite_redstone2h",  offsetof(struct GGame, sprite_redstone2h),  0  },
    { "sprite_redstone1v",  offsetof(struct GGame, sprite_redstone1v),  0  },
    { "sprite_redstone2v",  offsetof(struct GGame, sprite_redstone2v),  0  },
    { "sprite_redstone3v",  offsetof(struct GGame, sprite_redstone3v),  0  },
    { "sprite_redstone1hv", offsetof(struct GGame, sprite_redstone1hv), 0  },
    { "sprite_redstone2hv", offsetof(struct GGame, sprite_redstone2hv), 0  },
    { "sprite_backleft1",   offsetof(struct GGame, sprite_backleft1),   0  },
    { "sprite_backleft2",   offsetof(struct GGame, sprite_backleft2),   0  },
    { "sprite_backright1",  offsetof(struct GGame, sprite_backright1),  0  },
    { "sprite_backright2",  offsetof(struct GGame, sprite_backright2),  0  },
    { "sprite_backtop1",    offsetof(struct GGame, sprite_backtop1),    0  },
    { "sprite_backvmid1",   offsetof(struct GGame, sprite_backvmid1),   0  },
    { "sprite_backvmid2",   offsetof(struct GGame, sprite_backvmid2),   0  },
    { "sprite_backvmid3",   offsetof(struct GGame, sprite_backvmid3),   0  },
};
#define SPRITE_TABLE_COUNT (sizeof(sprite_table) / sizeof(sprite_table[0]))

static struct DashSprite*
get_sprite_from_game(
    struct GGame* game,
    size_t offset,
    int index)
{
    if( index < 0 )
        return *(struct DashSprite**)((char*)game + offset);
    return ((struct DashSprite**)((char*)game + offset))[index];
}

/* Return offset and set *out_index to -1 or array index. Returns (size_t)-1 if not found. */
static size_t
lookup_sprite_key(
    const char* key,
    int* out_index)
{
    *out_index = -1;
    char base[INI_MAX_KEY];
    int idx = -1;
    const char* p = strchr(key, '[');
    if( p && p[1] >= '0' && p[1] <= '9' )
    {
        size_t n = (size_t)(p - key);
        if( n >= sizeof(base) )
            return (size_t)-1;
        memcpy(base, key, n);
        base[n] = '\0';
        idx = (int)strtol(p + 1, NULL, 10);
    }
    else
    {
        strncpy(base, key, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
    }
    for( size_t i = 0; i < SPRITE_TABLE_COUNT; i++ )
    {
        if( strcmp(sprite_table[i].key, base) == 0 )
        {
            if( idx >= 0 )
            {
                if( idx >= sprite_table[i].array_size )
                    return (size_t)-1;
                *out_index = idx;
            }
            return sprite_table[i].offset;
        }
    }
    return (size_t)-1;
}

/* ---- Position expression ---- */
static int
parse_position_type(
    const char* s,
    int* out_value)
{
    *out_value = 0;
    if( strcmp(s, "chat_y") == 0 )
        return STATIC_UI_POS_CHAT_Y;
    if( strcmp(s, "bind_x") == 0 )
        return STATIC_UI_POS_BIND_X;
    if( strcmp(s, "bind_y") == 0 )
        return STATIC_UI_POS_BIND_Y;
    if( strcmp(s, "bind_bottom_x") == 0 )
        return STATIC_UI_POS_BIND_BOTTOM_X;
    if( strcmp(s, "bind_bottom_y") == 0 )
        return STATIC_UI_POS_BIND_BOTTOM_Y;
    if( strcmp(s, "chat_y_m19") == 0 )
        return STATIC_UI_POS_CHAT_Y_M19;
    if( isdigit((unsigned char)*s) || (*s == '-' && isdigit((unsigned char)s[1])) )
    {
        *out_value = (int)strtol(s, NULL, 10);
        return STATIC_UI_POS_LITERAL;
    }
    return STATIC_UI_POS_LITERAL;
}

int
static_ui_resolve_x(
    const struct StaticUiElementDescriptor* d,
    const struct StaticUiFrameContext* ctx)
{
    switch( d->x_type )
    {
    case STATIC_UI_POS_LITERAL:
        return d->x_value;
    case STATIC_UI_POS_CHAT_Y:
        return ctx->chat_y;
    case STATIC_UI_POS_BIND_X:
        return ctx->bind_x;
    case STATIC_UI_POS_BIND_Y:
        return ctx->bind_y;
    case STATIC_UI_POS_BIND_BOTTOM_X:
        return ctx->bind_bottom_x;
    case STATIC_UI_POS_BIND_BOTTOM_Y:
        return ctx->bind_bottom_y;
    case STATIC_UI_POS_CHAT_Y_M19:
        return ctx->chat_y - 19;
    default:
        return 0;
    }
}

int
static_ui_resolve_y(
    const struct StaticUiElementDescriptor* d,
    const struct StaticUiFrameContext* ctx)
{
    switch( d->y_type )
    {
    case STATIC_UI_POS_LITERAL:
        return d->y_value;
    case STATIC_UI_POS_CHAT_Y:
        return ctx->chat_y;
    case STATIC_UI_POS_BIND_X:
        return ctx->bind_x;
    case STATIC_UI_POS_BIND_Y:
        return ctx->bind_y;
    case STATIC_UI_POS_BIND_BOTTOM_X:
        return ctx->bind_bottom_x;
    case STATIC_UI_POS_BIND_BOTTOM_Y:
        return ctx->bind_bottom_y;
    case STATIC_UI_POS_CHAT_Y_M19:
        return ctx->chat_y - 19;
    default:
        return 0;
    }
}

/* ---- State handlers ---- */
static void
state_tab_redstone_top(
    struct GGame* game,
    const struct StaticUiFrameContext* ctx,
    int* out_visible,
    struct DashSprite** out_sprite,
    int* out_x,
    int* out_y,
    int* out_rotation_cw)
{
    (void)out_rotation_cw;
    *out_visible = 0;
    *out_sprite = NULL;
    if( game->sidebar_interface_id != -1 || game->selected_tab < 0 || game->selected_tab > 6 )
        return;
    struct DashSprite* r = NULL;
    int rx = 0, ry = 0;
    switch( game->selected_tab )
    {
    case 0:
        r = game->sprite_redstone1;
        rx = 22;
        ry = 10;
        break;
    case 1:
        r = game->sprite_redstone2;
        rx = 54;
        ry = 8;
        break;
    case 2:
        r = game->sprite_redstone2;
        rx = 82;
        ry = 8;
        break;
    case 3:
        r = game->sprite_redstone3;
        rx = 110;
        ry = 8;
        break;
    case 4:
        r = game->sprite_redstone2h;
        rx = 153;
        ry = 8;
        break;
    case 5:
        r = game->sprite_redstone2h;
        rx = 181;
        ry = 8;
        break;
    case 6:
        r = game->sprite_redstone1h;
        rx = 209;
        ry = 9;
        break;
    default:
        break;
    }
    if( !r )
        return;
    *out_visible = 1;
    *out_sprite = r;
    *out_x = ctx->bind_x + rx;
    *out_y = ctx->bind_y + ry;
}

static void
state_tab_redstone_bottom(
    struct GGame* game,
    const struct StaticUiFrameContext* ctx,
    int* out_visible,
    struct DashSprite** out_sprite,
    int* out_x,
    int* out_y,
    int* out_rotation_cw)
{
    (void)out_rotation_cw;
    *out_visible = 0;
    *out_sprite = NULL;
    if( game->sidebar_interface_id != -1 || game->selected_tab < 7 || game->selected_tab > 13 )
        return;
    struct DashSprite* r = NULL;
    int rx = 0, ry = 0;
    switch( game->selected_tab )
    {
    case 7:
        r = game->sprite_redstone1v;
        rx = 42;
        ry = 0;
        break;
    case 8:
        r = game->sprite_redstone2v;
        rx = 74;
        ry = 0;
        break;
    case 9:
        r = game->sprite_redstone2v;
        rx = 102;
        ry = 0;
        break;
    case 10:
        r = game->sprite_redstone3v;
        rx = 130;
        ry = 1;
        break;
    case 11:
        r = game->sprite_redstone2hv;
        rx = 173;
        ry = 0;
        break;
    case 12:
        r = game->sprite_redstone2hv;
        rx = 201;
        ry = 0;
        break;
    case 13:
        r = game->sprite_redstone1hv;
        rx = 229;
        ry = 0;
        break;
    default:
        break;
    }
    if( !r )
        return;
    *out_visible = 1;
    *out_sprite = r;
    *out_x = ctx->bind_bottom_x + rx;
    *out_y = ctx->bind_bottom_y + ry;
}

static void
state_compass(
    struct GGame* game,
    const struct StaticUiFrameContext* ctx,
    int* out_visible,
    struct DashSprite** out_sprite,
    int* out_x,
    int* out_y,
    int* out_rotation_cw)
{
    (void)ctx;
    *out_visible = (game->sprite_compass != NULL);
    *out_sprite = game->sprite_compass;
    *out_x = 550;
    *out_y = 4;
    *out_rotation_cw = game->camera_yaw;
}

static StaticUiStateHandlerFn
lookup_state_handler(const char* name)
{
    if( strcmp(name, "tab_redstone_top") == 0 )
        return state_tab_redstone_top;
    if( strcmp(name, "tab_redstone_bottom") == 0 )
        return state_tab_redstone_bottom;
    if( strcmp(name, "compass") == 0 )
        return state_compass;
    return NULL;
}

/* ---- Minimal INI parser ---- */
static void
trim(char* s)
{
    size_t len = strlen(s);
    while( len > 0 && (unsigned char)s[len - 1] <= ' ' )
        s[--len] = '\0';
    while( *s && (unsigned char)*s <= ' ' )
        memmove(s, s + 1, strlen(s) + 1);
}

static int
parse_ini(
    const char* path,
    void* ud,
    int (*on_section)(
        void* ud,
        const char* section_id),
    int (*on_keyval)(
        void* ud,
        const char* key,
        const char* value))
{
    FILE* f = fopen(path, "r");
    if( !f )
        return -1;
    char line[MAX_LINE];
    char current_section[INI_MAX_KEY] = "";
    int in_section = 0;
    int err = 0;
    while( fgets(line, sizeof(line), f) && !err )
    {
        trim(line);
        if( !line[0] || line[0] == ';' || line[0] == '#' )
            continue;
        if( line[0] == '[' )
        {
            char* close = strchr(line, ']');
            if( close )
            {
                *close = '\0';
                strncpy(current_section, line + 1, INI_MAX_KEY - 1);
                current_section[INI_MAX_KEY - 1] = '\0';
                trim(current_section);
                in_section = 1;
                if( on_section && on_section(ud, current_section) != 0 )
                    err = 1;
            }
            continue;
        }
        if( !in_section )
            continue;
        char* eq = strchr(line, '=');
        if( !eq )
            continue;
        *eq = '\0';
        char* key = line;
        char* val = eq + 1;
        trim(key);
        trim(val);
        if( on_keyval && on_keyval(ud, key, val) != 0 )
            err = 1;
    }
    fclose(f);
    return err ? -1 : 0;
}

/* Parser state for building descriptors */
struct ParseState
{
    struct StaticUiElementDescriptor* descriptors;
    int* count;
    char current_id[STATIC_UI_MAX_ID_LEN];
    char current_sprite[INI_MAX_VAL];
    char current_x[INI_MAX_VAL];
    char current_y[INI_MAX_VAL];
    char current_state_handler[STATIC_UI_MAX_HANDLER_NAME_LEN];
    char current_draw_type[16];
    int has_sprite;
};

static int
on_section_cb(
    void* ud,
    const char* section_id)
{
    struct ParseState* st = (struct ParseState*)ud;
    if( st->has_sprite && st->current_sprite[0] )
    {
        if( *st->count >= MAX_DESCRIPTORS )
            return -1;
        struct StaticUiElementDescriptor* d = &st->descriptors[*st->count];
        memset(d, 0, sizeof(*d));
        strncpy(d->id, st->current_id, STATIC_UI_MAX_ID_LEN - 1);
        int idx = -1;
        size_t off = lookup_sprite_key(st->current_sprite, &idx);
        if( off == (size_t)-1 )
            return -1;
        d->sprite_offset = off;
        d->sprite_index = idx;
        d->x_type = parse_position_type(st->current_x, &d->x_value);
        d->y_type = parse_position_type(st->current_y, &d->y_value);
        if( st->current_state_handler[0] )
        {
            strncpy(
                d->state_handler_name,
                st->current_state_handler,
                STATIC_UI_MAX_HANDLER_NAME_LEN - 1);
            d->state_handler = lookup_state_handler(d->state_handler_name);
        }
        if( strcmp(st->current_draw_type, "sprite_rotated") == 0 )
        {
            d->draw_type = STATIC_UI_DRAW_TYPE_SPRITE_ROTATED;
            d->rotated_dest_w = 33;
            d->rotated_dest_h = 33;
            d->rotated_dest_anchor_x = 16;
            d->rotated_dest_anchor_y = 16;
        }
        else
            d->draw_type = STATIC_UI_DRAW_TYPE_SPRITE;
        (*st->count)++;
    }
    strncpy(st->current_id, section_id, STATIC_UI_MAX_ID_LEN - 1);
    st->current_id[STATIC_UI_MAX_ID_LEN - 1] = '\0';
    st->current_sprite[0] = '\0';
    st->current_x[0] = '\0';
    st->current_y[0] = '\0';
    st->current_state_handler[0] = '\0';
    st->current_draw_type[0] = '\0';
    st->has_sprite = 0;
    return 0;
}

static int
on_keyval_cb(
    void* ud,
    const char* key,
    const char* value)
{
    struct ParseState* st = (struct ParseState*)ud;
    if( strcmp(key, "sprite") == 0 )
    {
        strncpy(st->current_sprite, value, INI_MAX_VAL - 1);
        st->current_sprite[INI_MAX_VAL - 1] = '\0';
        st->has_sprite = 1;
    }
    else if( strcmp(key, "x") == 0 )
        strncpy(st->current_x, value, INI_MAX_VAL - 1);
    else if( strcmp(key, "y") == 0 )
        strncpy(st->current_y, value, INI_MAX_VAL - 1);
    else if( strcmp(key, "state_handler") == 0 )
        strncpy(st->current_state_handler, value, STATIC_UI_MAX_HANDLER_NAME_LEN - 1);
    else if( strcmp(key, "draw_type") == 0 )
        strncpy(st->current_draw_type, value, sizeof(st->current_draw_type) - 1);
    return 0;
}

/* ---- Order file: one element id per line ---- */
static int
parse_order_file(
    const char* path,
    const struct StaticUiElementDescriptor* descriptors,
    int descriptor_count,
    struct StaticUiElementCommand* commands,
    int* out_count,
    int capacity)
{
    FILE* f = fopen(path, "r");
    if( !f )
        return -1;
    char line[MAX_LINE];
    *out_count = 0;
    while( fgets(line, sizeof(line), f) && *out_count < capacity )
    {
        trim(line);
        if( !line[0] || line[0] == '#' )
            continue;
        for( int i = 0; i < descriptor_count; i++ )
        {
            if( strcmp(descriptors[i].id, line) == 0 )
            {
                commands[*out_count].descriptor = &descriptors[i];
                (*out_count)++;
                break;
            }
        }
    }
    fclose(f);
    return 0;
}

/* ---- Load ---- */
struct StaticUiElementCommandBuffer*
static_ui_buffer_load(
    const char* ini_path,
    const char* order_path)
{
    struct StaticUiElementDescriptor* descriptors =
        malloc(MAX_DESCRIPTORS * sizeof(struct StaticUiElementDescriptor));
    if( !descriptors )
        return NULL;
    int descriptor_count = 0;
    struct ParseState pst = {
        .descriptors = descriptors,
        .count = &descriptor_count,
        .current_id = "",
        .current_sprite = "",
        .current_x = "",
        .current_y = "",
        .current_state_handler = "",
        .current_draw_type = "",
        .has_sprite = 0,
    };
    if( parse_ini(ini_path, &pst, on_section_cb, on_keyval_cb) != 0 )
    {
        free(descriptors);
        return NULL;
    }
    on_section_cb(&pst, ""); /* flush last section */
    if( descriptor_count == 0 )
    {
        free(descriptors);
        return NULL;
    }
    int order_capacity = 256;
    struct StaticUiElementCommand* commands =
        malloc(order_capacity * sizeof(struct StaticUiElementCommand));
    if( !commands )
    {
        free(descriptors);
        return NULL;
    }
    int command_count = 0;
    if( parse_order_file(
            order_path, descriptors, descriptor_count, commands, &command_count, order_capacity) !=
        0 )
    {
        free(commands);
        free(descriptors);
        return NULL;
    }
    struct StaticUiElementCommandBuffer* buffer =
        malloc(sizeof(struct StaticUiElementCommandBuffer));
    if( !buffer )
    {
        free(commands);
        free(descriptors);
        return NULL;
    }
    buffer->descriptors = descriptors;
    buffer->descriptor_count = descriptor_count;
    buffer->commands = commands;
    buffer->count = command_count;
    buffer->capacity = order_capacity;
    return buffer;
}

void
static_ui_buffer_free(struct StaticUiElementCommandBuffer* buffer)
{
    if( !buffer )
        return;
    free(buffer->descriptors);
    free(buffer->commands);
    free(buffer);
}

/* ---- Execute ---- */
void
static_ui_buffer_execute(
    struct StaticUiElementCommandBuffer* buffer,
    struct GGame* game,
    const struct StaticUiFrameContext* frame_ctx,
    const struct StaticUiRenderCallbacks* callbacks)
{
    if( !buffer || !callbacks || !callbacks->blit_sprite )
        return;
    for( int i = 0; i < buffer->count; i++ )
    {
        const struct StaticUiElementDescriptor* d = buffer->commands[i].descriptor;
        int visible = 1;
        struct DashSprite* sprite = NULL;
        int x = 0, y = 0;
        int rotation_cw = 0;
        if( d->state_handler )
        {
            d->state_handler(game, frame_ctx, &visible, &sprite, &x, &y, &rotation_cw);
        }
        else
        {
            sprite = get_sprite_from_game(game, d->sprite_offset, d->sprite_index);
            x = static_ui_resolve_x(d, frame_ctx);
            y = static_ui_resolve_y(d, frame_ctx);
        }
        if( !visible || !sprite )
            continue;
        if( d->draw_type == STATIC_UI_DRAW_TYPE_SPRITE_ROTATED && callbacks->blit_rotated )
            callbacks->blit_rotated(
                callbacks->userdata,
                game,
                sprite,
                x,
                y,
                d->rotated_dest_w,
                d->rotated_dest_h,
                d->rotated_dest_anchor_x,
                d->rotated_dest_anchor_y,
                rotation_cw);
        else
            callbacks->blit_sprite(callbacks->userdata, game, sprite, x, y);
    }
}
