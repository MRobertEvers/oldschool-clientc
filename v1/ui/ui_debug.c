#include "ui_debug.h"

#include "osrs/minimenu_action.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "ui_behavior.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

bool
ui_hover_debug_enabled(void)
{
    static int cached = -1;
    if( cached < 0 )
    {
        char const* env = getenv("UI_HOVER_DEBUG");
        cached = (env && strcmp(env, "1") == 0) ? 1 : 0;
    }
    return cached != 0;
}

void
ui_hover_debug_log(
    char const* fmt,
    ...)
{
    assert(fmt);
    if( !ui_hover_debug_enabled() )
        return;

    va_list args;
    va_start(args, fmt);
    fputs("ui_hover: ", stderr);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
}

bool
ui_active_debug_enabled(void)
{
    static int cached = -1;
    if( cached < 0 )
    {
        char const* env = getenv("UI_ACTIVE_DEBUG");
        cached = (env && strcmp(env, "1") == 0) ? 1 : 0;
    }
    return cached != 0;
}

void
ui_active_debug_log(
    char const* fmt,
    ...)
{
    assert(fmt);
    if( !ui_active_debug_enabled() )
        return;

    va_list args;
    va_start(args, fmt);
    fputs("ui_active: ", stderr);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
}

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
ui_minimenu_debug_log(
    char const* fmt,
    ...)
{
    assert(fmt);
    if( !ui_minimenu_debug_enabled() )
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
    case UIELEM_BUILTIN_CHAT_BUTTON:
        return "UIELEM_BUILTIN_CHAT_BUTTON";
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
    case UIELEM_INV_GRID:
        return "UIELEM_INV_GRID";
    case UIELEM_INV_SLOT:
        return "UIELEM_INV_SLOT";
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
    assert(out);
    if( out_size == 0 )
        return;

    out[0] = '\0';
    size_t used = 0;
    for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
    {
        if( ops[i][0] == '\0' )
            continue;

        int n = snprintf(
            out + used, out_size - used, "%s[%d]='%s'", used == 0 ? prefix : " ", i, ops[i]);
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

static int
ui_minimenu_debug_count_nonempty_ops(
    char const ops[UITREE_MENU_OPTION_SLOTS][UITREE_MENU_OPTION_LEN])
{
    int count = 0;
    for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
    {
        if( ops[i][0] != '\0' )
            count++;
    }
    return count;
}

static void
ui_minimenu_debug_format_inv_item_option(
    char* out,
    size_t out_size,
    char const* verb,
    char const* obj_name)
{
    snprintf(out, out_size, "%s @lre@ %s", verb, obj_name);
}

static void
ui_minimenu_debug_append_inv_item_opcodes(
    char* out,
    size_t out_size,
    struct ToriAuxLibCore_Objtype const* obj)
{
    assert(out);
    if( out_size == 0 )
        return;

    out[0] = '\0';
    size_t used = 0;
    char const* obj_name = (obj && obj->name[0] != '\0') ? obj->name : "item";
    char label[UITREE_MENU_OPTION_LEN];

    for( int op = 4; op >= 3; op-- )
    {
        char const* verb = NULL;
        enum MinimenuAction action = MINIMENU_ACTION_CANCEL;
        if( obj && obj->inv_actions[op][0] != '\0' )
        {
            verb = obj->inv_actions[op];
            action = (enum MinimenuAction)(MINIMENU_ACTION_OPHELD1 + op);
        }
        else if( op == 4 )
        {
            verb = "Drop";
            action = MINIMENU_ACTION_OPHELD5;
        }
        else
        {
            continue;
        }

        ui_minimenu_debug_format_inv_item_option(label, sizeof(label), verb, obj_name);
        int n = snprintf(
            out + used,
            out_size - used,
            "%s[%d]:'%s'->%d",
            used == 0 ? "item_opcodes=" : " ",
            op,
            label,
            (int)action);
        if( n < 0 || (size_t)n >= out_size - used )
            break;
        used += (size_t)n;
    }

    ui_minimenu_debug_format_inv_item_option(label, sizeof(label), "Use", obj_name);
    int n = snprintf(
        out + used,
        out_size - used,
        "%s'%s'->%d",
        used == 0 ? "item_opcodes=" : " ",
        label,
        (int)MINIMENU_ACTION_OPHELDT_START);
    if( n > 0 && (size_t)n < out_size - used )
        used += (size_t)n;

    for( int op = 2; op >= 0; op-- )
    {
        assert(obj);
        if( obj->inv_actions[op][0] == '\0' )
            continue;

        ui_minimenu_debug_format_inv_item_option(
            label, sizeof(label), obj->inv_actions[op], obj_name);
        n = snprintf(
            out + used,
            out_size - used,
            " [%d]:'%s'->%d",
            op,
            label,
            (int)(MINIMENU_ACTION_OPHELD1 + op));
        if( n < 0 || (size_t)n >= out_size - used )
            break;
        used += (size_t)n;
    }

    ui_minimenu_debug_format_inv_item_option(label, sizeof(label), "Examine", obj_name);
    n = snprintf(out + used, out_size - used, " '%s'->%d", label, (int)MINIMENU_ACTION_OPHELD6);
    if( n > 0 && (size_t)n < out_size - used )
        used += (size_t)n;

    if( used == 0 )
        snprintf(out, out_size, "item_opcodes=(none)");
}

static void
ui_minimenu_debug_append_inv_container_opcodes(
    char* out,
    size_t out_size,
    struct StaticUIMenuOptions const* opts,
    char const* obj_name)
{
    assert(out);
    if( out_size == 0 || !opts )
        return;

    out[0] = '\0';
    size_t used = 0;
    char label[UITREE_MENU_OPTION_LEN];
    char const* name = (obj_name && obj_name[0] != '\0') ? obj_name : "item";

    for( int i = 4; i >= 0; i-- )
    {
        if( opts->ops[i][0] == '\0' )
            continue;

        enum MinimenuAction action = (enum MinimenuAction)(MINIMENU_ACTION_INV_BUTTON1 + i);
        if( opts->op_actions[i] != 0 )
            action = (enum MinimenuAction)opts->op_actions[i];

        ui_minimenu_debug_format_inv_item_option(label, sizeof(label), opts->ops[i], name);
        int n = snprintf(
            out + used,
            out_size - used,
            "%s[%d]:'%s'->%d",
            used == 0 ? "container_opcodes=" : " ",
            i,
            label,
            (int)action);
        if( n < 0 || (size_t)n >= out_size - used )
            break;
        used += (size_t)n;
    }
    if( used == 0 )
        snprintf(out, out_size, "container_opcodes=(none)");
}

void
ui_minimenu_debug_log_inv_slot_ops(
    char const* tag,
    struct MinimenuPick const* pick,
    struct StaticUIComponent const* inv_component,
    struct ToriAuxLibCore_Objtype const* obj,
    int menu_rows_added,
    int selected_action,
    int selected_action_index)
{
    if( !ui_minimenu_debug_enabled() || !pick )
        return;

    int const item_ops = obj ? ui_minimenu_debug_count_nonempty_ops(obj->inv_actions) : 0;
    int const container_ops =
        inv_component ? ui_minimenu_debug_count_nonempty_ops(inv_component->menu_options.ops) : 0;

    char item_opcode_buf[384];
    char container_opcode_buf[384];
    char const* obj_name = (obj && obj->name[0] != '\0') ? obj->name : "item";
    ui_minimenu_debug_append_inv_item_opcodes(item_opcode_buf, sizeof(item_opcode_buf), obj);
    if( inv_component )
    {
        ui_minimenu_debug_append_inv_container_opcodes(
            container_opcode_buf,
            sizeof(container_opcode_buf),
            &inv_component->menu_options,
            obj_name);
    }
    else
    {
        snprintf(container_opcode_buf, sizeof(container_opcode_buf), "container_opcodes=(none)");
    }

    int rs_id = inv_component ? inv_component->component_id : -1;
    int node_idx = pick->quaternary_id;

    if( selected_action != 0 )
    {
        ui_minimenu_debug_log(
            "%s inv_index=%d slot=%d obj_id=%d rs_id=%d node_idx=%d item_ops=%d %s "
            "container_ops=%d %s menu_rows_added=%d selected_action=%d "
            "selected_action_index=%d",
            tag ? tag : "inv_slot",
            pick->id,
            pick->secondary_id,
            pick->tertiary_id,
            rs_id,
            node_idx,
            item_ops,
            item_opcode_buf,
            container_ops,
            container_opcode_buf,
            menu_rows_added,
            selected_action,
            selected_action_index);
    }
    else
    {
        ui_minimenu_debug_log(
            "%s inv_index=%d slot=%d obj_id=%d rs_id=%d node_idx=%d item_ops=%d %s "
            "container_ops=%d %s menu_rows_added=%d",
            tag ? tag : "inv_slot",
            pick->id,
            pick->secondary_id,
            pick->tertiary_id,
            rs_id,
            node_idx,
            item_ops,
            item_opcode_buf,
            container_ops,
            container_opcode_buf,
            menu_rows_added);
    }
}
