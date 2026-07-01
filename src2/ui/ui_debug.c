#include "ui_debug.h"

#include "ui_behavior.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool
ui_minimenu_debug_enabled(void)
{
    static int cached = -1;
    if( cached < 0 )
    {
        char const* env = getenv("UI_MINIMENU_DEBUG");
        cached = (env && strcmp(env, "1") == 0) ? 1 : 0;
    }
    return cached != 0;
}

void
ui_minimenu_debug_log(char const* fmt, ...)
{
    if( !ui_minimenu_debug_enabled() || !fmt )
        return;

    va_list args;
    va_start(args, fmt);
    fputs("ui_minimenu: ", stderr);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
}

char const*
ui_minimenu_debug_uielem_name(int type)
{
    switch( type )
    {
    case UIELEM_BUILTIN_COMPASS:
        return "UIELEM_BUILTIN_COMPASS";
    case UIELEM_BUILTIN_MINIMAP:
        return "UIELEM_BUILTIN_MINIMAP";
    case UIELEM_BUILTIN_SIDEBAR:
        return "UIELEM_BUILTIN_SIDEBAR";
    case UIELEM_BUILTIN_CHAT:
        return "UIELEM_BUILTIN_CHAT";
    case UIELEM_BUILTIN_WORLD:
        return "UIELEM_BUILTIN_WORLD";
    case UIELEM_BUILTIN_SPRITE:
        return "UIELEM_BUILTIN_SPRITE";
    case UIELEM_BUILTIN_REDSTONE_TAB:
        return "UIELEM_BUILTIN_REDSTONE_TAB";
    case UIELEM_BUILTIN_TAB_ICONS:
        return "UIELEM_BUILTIN_TAB_ICONS";
    case UIELEM_BUILTIN_CROSS:
        return "UIELEM_BUILTIN_CROSS";
    case UIELEM_BUILTIN_MINIMENU:
        return "UIELEM_BUILTIN_MINIMENU";
    case UIELEM_RS_TEXT:
        return "UIELEM_RS_TEXT";
    case UIELEM_RS_GRAPHIC:
        return "UIELEM_RS_GRAPHIC";
    case UIELEM_RS_MODEL:
        return "UIELEM_RS_MODEL";
    case UIELEM_RS_INV:
        return "UIELEM_RS_INV";
    case UIELEM_RS_LAYER:
        return "UIELEM_RS_LAYER";
    case UIELEM_RS_RECT:
        return "UIELEM_RS_RECT";
    case UIELEM_RS_LINE:
        return "UIELEM_RS_LINE";
    case UIELEM_RS_INV_TEXT:
        return "UIELEM_RS_INV_TEXT";
    default:
        return "UIELEM_UNKNOWN";
    }
}

static void
ui_minimenu_debug_append_ops(
    char* out,
    size_t out_size,
    char const* prefix,
    char const ops[UITREE_MENU_OPTION_SLOTS][UITREE_MENU_OPTION_LEN])
{
    if( !out || out_size == 0 )
        return;

    out[0] = '\0';
    size_t used = 0;
    for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
    {
        if( ops[i][0] == '\0' )
            continue;

        int n = snprintf(
            out + used,
            out_size - used,
            "%s[%d]='%s'",
            used == 0 ? prefix : " ",
            i,
            ops[i]);
        if( n < 0 || (size_t)n >= out_size - used )
            break;
        used += (size_t)n;
    }
    if( used == 0 )
        snprintf(out, out_size, "%s=(none)", prefix);
}

void
ui_minimenu_debug_log_menu_options(
    char const* tag,
    int component_id,
    int node_idx,
    int type,
    struct StaticUIMenuOptions const* opts,
    int button_type,
    int client_code)
{
    if( !ui_minimenu_debug_enabled() || !opts )
        return;

    struct StaticUIComponent probe = {
        .type = (enum StaticUIComponentType)type,
        .menu_options = *opts,
        .behavior = {
            .button_type = button_type,
            .client_code = client_code,
        },
    };

    char ops_buf[256];
    ui_minimenu_debug_append_ops(ops_buf, sizeof(ops_buf), "ops", opts->ops);

    ui_minimenu_debug_log(
        "%s rs_id=%d node_idx=%d type=%s option='%s' %s button_type=%d client_code=%d "
        "has_menu_options=%d expects_rows=%d",
        tag ? tag : "menu_options",
        component_id,
        node_idx,
        ui_minimenu_debug_uielem_name(type),
        opts->option,
        ops_buf,
        button_type,
        client_code,
        uitree_component_has_menu_options(&probe),
        uitree_component_expects_minimenu_rows(&probe));
}
