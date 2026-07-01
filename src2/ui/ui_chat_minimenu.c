#include "ui_chat_minimenu.h"

#include "games/runescape.h"
#include "osrs/minimenu_action.h"
#include "ui/uitree.h"
#include "ui/uitree_layout.h"

#include <stdio.h>
#include <string.h>

int32_t
uitree_find_chat_builtin_node(struct UITree const* tree)
{
    if( !tree )
        return -1;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( tree->components[i].type == UIELEM_BUILTIN_CHAT )
            return (int32_t)i;
    }
    return -1;
}

static void
ui_chat_minimenu_format_template(
    char* out,
    size_t out_len,
    char const* tmpl,
    char const* sender)
{
    if( !out || out_len == 0 )
        return;
    out[0] = '\0';
    if( !tmpl || tmpl[0] == '\0' )
        return;

    char const* pct = strstr(tmpl, "%s");
    if( !pct )
    {
        snprintf(out, out_len, "%s", tmpl);
        return;
    }

    size_t const prefix_len = (size_t)(pct - tmpl);
    size_t copy_len = prefix_len;
    if( copy_len >= out_len )
        copy_len = out_len - 1;
    memcpy(out, tmpl, copy_len);
    out[copy_len] = '\0';
    snprintf(out + copy_len, out_len - copy_len, "%s", sender ? sender : "");
    char const* suffix = pct + 2;
    if( suffix[0] != '\0' )
    {
        size_t cur = strlen(out);
        snprintf(out + cur, out_len - cur, "%s", suffix);
    }
}

static void
ui_chat_minimenu_add_template_row(
    struct UIMinimenuState* menu,
    char const* tmpl,
    int action,
    int priority,
    char const* sender)
{
    if( !menu || !tmpl || tmpl[0] == '\0' || action == 0 )
        return;

    char text[UI_MINIMENU_OPTION_LEN];
    ui_chat_minimenu_format_template(text, sizeof(text), tmpl, sender);
    if( text[0] == '\0' )
        return;

    enum MinimenuAction row_action = (enum MinimenuAction)action;
    if( priority )
        row_action = (enum MinimenuAction)minimenu_action_priority(row_action, 1);

    ui_minimenu_add_option(menu, text, row_action, -1);
}

static bool
ui_chat_minimenu_sender_is_friend(struct GameRunescape const* game, char const* sender)
{
    if( !game || !sender || sender[0] == '\0' )
        return false;
    for( int i = 0; i < game->friend_count; i++ )
    {
        if( strcmp(game->friend_username[i], sender) == 0 )
            return true;
    }
    return false;
}

static bool
ui_chat_minimenu_line_visible_public(
    struct GameRunescape const* game,
    int chat_type,
    char const* sender)
{
    if( chat_type != 1 && chat_type != 2 )
        return false;
    if( game->chat_public_mode == 0 )
        return true;
    if( game->chat_public_mode == 1 )
        return ui_chat_minimenu_sender_is_friend(game, sender);
    return false;
}

static bool
ui_chat_minimenu_line_visible_private(
    struct GameRunescape const* game,
    int chat_type,
    char const* sender)
{
    if( chat_type != 3 && chat_type != 7 )
        return false;
    if( chat_type == 7 )
        return true;
    if( game->chat_private_mode == 0 )
        return true;
    if( game->chat_private_mode == 1 )
        return ui_chat_minimenu_sender_is_friend(game, sender);
    return false;
}

static bool
ui_chat_minimenu_line_visible_trade(
    struct GameRunescape const* game,
    int chat_type,
    char const* sender)
{
    if( chat_type != 4 && chat_type != 8 )
        return false;
    if( game->chat_trade_mode == 0 )
        return true;
    if( game->chat_trade_mode == 1 )
        return ui_chat_minimenu_sender_is_friend(game, sender);
    return false;
}

static void
ui_chat_minimenu_add_social_rows(
    struct GameRunescape* game,
    struct StaticUIChatMinimenuConfig const* config,
    struct UIMinimenuState* menu,
    char const* sender,
    int priority)
{
    if( !game || !config || !menu || !sender || sender[0] == '\0' )
        return;

    if( game->staff_mod_level >= 1 && config->op_report_abuse[0] != '\0' )
    {
        ui_chat_minimenu_add_template_row(
            menu,
            config->op_report_abuse,
            config->op_report_abuse_action,
            priority,
            sender);
    }

    if( config->op_add_ignore[0] != '\0' )
    {
        ui_chat_minimenu_add_template_row(
            menu,
            config->op_add_ignore,
            config->op_add_ignore_action,
            priority,
            sender);
    }

    if( config->op_add_friend[0] != '\0' )
    {
        ui_chat_minimenu_add_template_row(
            menu,
            config->op_add_friend,
            config->op_add_friend_action,
            priority,
            sender);
    }
}

static struct StaticUIChatMinimenuConfig const*
ui_chat_minimenu_resolve_config(struct GameRunescape* game)
{
    static struct StaticUIChatMinimenuConfig const k_defaults = {
        .op_report_abuse = "Report abuse @whi@%s",
        .op_report_abuse_action = MINIMENU_ACTION_REPORT_ABUSE,
        .op_add_ignore = "Add ignore @whi@%s",
        .op_add_ignore_action = MINIMENU_ACTION_IGNORELIST_ADD,
        .op_add_friend = "Add friend @whi@%s",
        .op_add_friend_action = MINIMENU_ACTION_FRIENDLIST_ADD,
        .op_accept_trade = "Accept trade @whi@%s",
        .op_accept_trade_action = MINIMENU_ACTION_OPPLAYER_TRADEREQ,
        .op_accept_duel = "Accept duel @whi@%s",
        .op_accept_duel_action = MINIMENU_ACTION_OPPLAYER_DUELREQ,
    };

    if( !game || !game->ui_tree )
        return &k_defaults;

    int32_t chat_idx = game->ui_chat_node;
    if( chat_idx < 0 || (uint32_t)chat_idx >= game->ui_tree->component_count ||
        game->ui_tree->components[chat_idx].type != UIELEM_BUILTIN_CHAT )
        chat_idx = uitree_find_chat_builtin_node(game->ui_tree);

    if( chat_idx < 0 )
        return &k_defaults;

    struct StaticUIChatMinimenuConfig const* baked =
        &game->ui_tree->components[chat_idx].u.chat.minimenu;
    if( baked->op_add_friend[0] != '\0' || baked->op_accept_trade[0] != '\0' )
        return baked;
    return &k_defaults;
}

void
ui_chat_minimenu_add_private_strip(
    struct GameRunescape* game,
    int mouse_x,
    int mouse_y,
    struct UIMinimenuState* menu)
{
    if( !game || !menu || game->split_private_chat == 0 )
        return;

    struct StaticUIChatMinimenuConfig const* config = ui_chat_minimenu_resolve_config(game);
    int line = game->reboot_timer != 0 ? 1 : 0;

    for( int i = 0; i < game->chat_line_count && i < RUNESCAPE_CHAT_LINE_MAX; i++ )
    {
        struct GameRunescape_ChatLine const* row = &game->chat_lines[i];
        if( row->text[0] == '\0' )
            continue;

        int const chat_type = row->type;
        if( !ui_chat_minimenu_line_visible_private(game, chat_type, row->username) )
        {
            if( (chat_type == 5 || chat_type == 6) && game->chat_private_mode < 2 )
                line++;
            continue;
        }

        int const y = 329 - line * 13;
        if( mouse_x > 4 && mouse_x < 516 && mouse_y - 4 > y - 10 && mouse_y - 4 <= y + 3 )
            ui_chat_minimenu_add_social_rows(game, config, menu, row->username, 1);

        line++;
        if( line >= 5 )
            return;
    }
}

void
ui_chat_minimenu_add_main_box(
    struct GameRunescape* game,
    int mouse_x,
    int mouse_y,
    struct StaticUIChatMinimenuConfig const* config,
    struct UIMinimenuState* menu)
{
    if( !game || !menu )
        return;

    if( !config )
        config = ui_chat_minimenu_resolve_config(game);

    for( int i = 0; i < game->chat_line_count && i < RUNESCAPE_CHAT_LINE_MAX; i++ )
    {
        struct GameRunescape_ChatLine const* row = &game->chat_lines[i];
        if( row->text[0] == '\0' )
            continue;

        int const chat_type = row->type;
        int line = 0;
        for( int j = 0; j < i; j++ )
        {
            int const prior_type = game->chat_lines[j].type;
            if( prior_type == 0 )
                line++;
            else if( prior_type == 1 || prior_type == 2 )
                line++;
            else if(
                (prior_type == 3 || prior_type == 7) && game->split_private_chat == 0 &&
                game->chat_private_mode < 2 )
                line++;
            else if( (prior_type == 4 || prior_type == 8) && game->chat_trade_mode < 2 )
                line++;
            else if(
                (prior_type == 5 || prior_type == 6) && game->split_private_chat == 0 &&
                game->chat_private_mode < 2 )
                line++;
        }

        int const y = game->chat_scroll_pos + 70 + 4 - line * 14;
        if( y < -20 )
            break;

        if( !(mouse_y > y - 14 && mouse_y <= y) )
            continue;

        if( ui_chat_minimenu_line_visible_public(game, chat_type, row->username) )
        {
            if( row->username[0] != '\0' )
                ui_chat_minimenu_add_social_rows(game, config, menu, row->username, 0);
        }
        else if(
            ui_chat_minimenu_line_visible_private(game, chat_type, row->username) &&
            game->split_private_chat == 0 )
        {
            ui_chat_minimenu_add_social_rows(game, config, menu, row->username, 0);
        }
        else if( chat_type == 4 && ui_chat_minimenu_line_visible_trade(game, chat_type, row->username) )
        {
            if( config->op_accept_trade[0] != '\0' )
            {
                ui_chat_minimenu_add_template_row(
                    menu,
                    config->op_accept_trade,
                    config->op_accept_trade_action,
                    0,
                    row->username);
            }
        }
        else if( chat_type == 8 && ui_chat_minimenu_line_visible_trade(game, chat_type, row->username) )
        {
            if( config->op_accept_duel[0] != '\0' )
            {
                ui_chat_minimenu_add_template_row(
                    menu,
                    config->op_accept_duel,
                    config->op_accept_duel_action,
                    0,
                    row->username);
            }
        }
    }

    (void)mouse_x;
}
